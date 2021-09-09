/*
* Copyright 2021 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Oberon+ parser/compiler library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
*/

#include "ObxCilGen.h"
#include "ObxAst.h"
#include "ObErrors.h"
#include "ObxProject.h"
#include "ObxIlEmitter.h"
#include "ObxPelibGen.h"
#include <QtDebug>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
using namespace Obx;
using namespace Ob;

#ifndef OBX_AST_DECLARE_SET_METATYPE_IN_HEADER
Q_DECLARE_METATYPE( Obx::Literal::SET )
#endif

#define _MY_GENERICS_ // using my own generics implementation instead of the dotnet generics;
                      // TODO there is an architectural value type initialization issue with dotnet generics!

// NOTE: mono (and .Net 4) ILASM and runtime error messages are of very little use, or even counter productive in that
// they often point in the wrong direction.

// NOTE: even though CoreCLR replaced mscorlib by System.Private.CoreLib the generated code still runs with "dotnet Main.exe",
// but the directory with the OBX assemblies requires a Main.runtimeconfig.json file as generated below
// "dotnet.exe run" apparently creates an non-managed exe which loads coreclr.dll and the app assembly dll; mono5 (in contrast to 3)
// is able to disasm and even run the app assembly dll created by dotnet.exe CoreCLR 3.1.

struct ObxCilGenCollector : public AstVisitor
{
    QList<Procedure*> allProcs;
    QList<Record*> allRecords;
    QSet<Module*> allImports;
    QList<ProcType*> allProcTypes;
    Module* thisMod;

    void collect(Type* t)
    {
        switch( t->getTag() )
        {
        case Thing::T_Array:
            collect(cast<Array*>(t)->d_type.data());
            break;
        case Thing::T_Record:
            {
                Record* r = cast<Record*>(t);
                allRecords.append( r );
                foreach( const Ref<Field>& f, r->d_fields )
                {
                    collect(f->d_type.data());
                }
                if( r->d_base )
                    collect(r->d_base.data());
            }
            break;
        case Thing::T_Pointer:
            collect(cast<Pointer*>(t)->d_to.data());
            break;
        case Thing::T_ProcType:
            {
                ProcType* pt = cast<ProcType*>(t);
                allProcTypes.append(pt);
                foreach( const Ref<Parameter>& p, pt->d_formals )
                    collect(p->d_type.data());
                if( pt->d_return )
                    collect(pt->d_return.data());
            }
            break;
        case Thing::T_QualiType:
            if( Record* r = t->toRecord() )
            {
                Named* n = r->findDecl();
                if( n ) // actually n cannot be 0
                    allImports.insert(n->getModule());
            }
#if 0
            // no, we only create delegates for proc types declared here
            else
            {
                t = t->derefed();
                if( t && t->getTag() == Thing::T_ProcType )
                    collect(t); // even if the proc type was declared in another module, we create a local delegate here
            }
#endif
            break;
        }
    }

    void collect( Named* n )
    {
        switch( n->getTag() )
        {
        case Thing::T_Procedure:
            {
                Procedure* p = cast<Procedure*>(n);
                if( p->d_receiver.isNull() )
                    allProcs.append(cast<Procedure*>(n));
                p->accept(this);
            }
            break;
        case Thing::T_NamedType:
            collect(n->d_type.data());
            break;
        case Thing::T_Variable:
        case Thing::T_Parameter:
        case Thing::T_LocalVar:
            collect(n->d_type.data());
            break;
        }
    }

    void visit( Module* me )
    {
        thisMod = me;
        foreach( const Ref<Named>& n, me->d_order )
            collect(n.data());
    }

    void visit( Procedure* me)
    {
        foreach( const Ref<Named>& n, me->d_order )
            collect(n.data());
    }
};

struct CilGenTempPool
{
    enum { MAX_TEMP = 32 };
    std::bitset<MAX_TEMP> d_slots;
    quint16 d_start;
    qint16 d_max; // max used slot
    CilGenTempPool():d_start(0),d_max(-1){}
    void reset(quint16 start)
    {
        d_slots.reset();
        d_start = start;
        d_max = -1;
    }
    int buy()
    {
        for( int i = 0; i < MAX_TEMP; i++ )
        {
            if( !d_slots.test(i) )
            {
                d_slots.set(i);
                if( i > d_max )
                    d_max = i;
                return i + d_start;
            }
        }
        Q_ASSERT( false );
        return -1;
    }
    void sell( int i )
    {
        Q_ASSERT( i >= d_start );
        d_slots.set(i-d_start,false);
    }
};

struct ObxCilGenImp : public AstVisitor
{
    Errors* err;
    Module* thisMod;
    IlEmitter* emitter;
    QString buffer;
    quint32 anonymousDeclNr; // starts with one, zero is an invalid slot
    qint16 level;
    bool ownsErr;
    bool forceAssemblyPrefix;
    bool forceFormalIndex;
    RowCol last;
    CilGenTempPool temps;
    QHash<QByteArray, QPair<Array*,int> > copiers; // type string -> array, max dim count
    QHash<QByteArray,ProcType*> delegates; // signature hash -> signature
    int exitJump; // TODO: nested LOOPs
    Procedure* scope;

    ObxCilGenImp():ownsErr(false),err(0),thisMod(0),anonymousDeclNr(1),level(0),
        exitJump(-1),scope(0),forceAssemblyPrefix(false),forceFormalIndex(false)
    {
    }

    inline QByteArray ws() { return QByteArray(level*4,' '); }

    static QByteArray inline escape( const QByteArray& name )
    {
        return "'" + name + "'";
    }

    QByteArray dottedName( Named* n, bool doEscape = true )
    {
        // concatenate names up to but not including module
        QByteArray name = doEscape ? escape(n->d_name) : n->d_name;
        if( n->d_scope && n->d_scope->getTag() != Thing::T_Module )
            return dottedName(n->d_scope,doEscape) + "." + name;
        return name;
    }

    QByteArray formatMetaActuals(Module* m)
    {
#ifndef _MY_GENERICS_
        if( m == thisMod && !m->d_metaParams.isEmpty() )
        {
            Q_ASSERT( m->d_metaActuals.isEmpty() );
            QByteArray res = "<";
            for( int i = 0; i < m->d_metaParams.size(); i++ )
            {
                if( i != 0 )
                    res += ",";
                Q_ASSERT( m->d_metaParams[i]->d_slotValid );
                res += "!" + QByteArray::number(m->d_metaParams[i]->d_slot);
            }
            res += ">";
            return res;
        }else if( !m->d_metaActuals.isEmpty() )
        {
            QByteArray res = "<";
            for( int i = 0; i < m->d_metaActuals.size(); i++ )
            {
                if( i != 0 )
                    res += ",";
                res += formatType(m->d_metaActuals[i].data());
            }
            res += ">";
            return res;
        }
#endif
        return QByteArray();
    }

    QByteArray formatMetaActuals(Type* t)
    {
        // t==0 -> module
        Module* m = 0;
        t = derefed(t);
        if( t == 0 )
            m = thisMod;
        else
            m = t->declaredIn();

        return formatMetaActuals(m);
    }

    inline QByteArray getName( Named* n )
    {
        Q_ASSERT(n);
#ifdef _MY_GENERICS_
        return n->getName();
#else
        if( n->getTag() == Thing::T_Module )
            return cast<Module*>(n)->d_fullName.join('.');
        else
            return n->d_name;
#endif
    }

    QByteArray moduleRef( Named* modName )
    {
        const QByteArray mod = escape(getName(modName));
        if( !forceAssemblyPrefix && modName == thisMod )
            return mod;
        else
            return "[" + mod + "]" + mod;
    }

    QByteArray classRef( Named* className )
    {
        Q_ASSERT( className && className->getTag() == Thing::T_NamedType );
        Module* m = className->getModule();
        return moduleRef(m) + "/" + dottedName(className);
    }

    QByteArray classRef( Record* r )
    {
        Named* n = r->findDecl();
        if( n && n->getTag() == Thing::T_NamedType )
            return classRef(n);
        else
        {
            Q_ASSERT( r->d_slotValid );
            if( n == 0 )
                n = r->findDecl(true);
            Module* m = n ? n->getModule() : 0;
            if( m == 0 )
                m = thisMod;
            return moduleRef(m) + "/'#" + QByteArray::number(r->d_slot) + "'";
        }
    }

    QByteArray memberRef( Named* member )
    {
        QByteArray res;
        Record* record = 0;
        ProcType* pt = 0;
        switch( member->getTag() )
        {
        case Thing::T_Field:
            {
                Field* f = cast<Field*>(member);
                record = f->d_owner;
            }
            break;
        case Thing::T_Variable:
            break;
        case Thing::T_Procedure:
            {
                Procedure* p = cast<Procedure*>(member);
                if( p->d_receiverRec )
                    record = p->d_receiverRec;
                pt = p->getProcType();
            }
            break;
        default:
            Q_ASSERT(false);
        }
        const QByteArray ma = formatMetaActuals(record);
        forceFormalIndex = !ma.isEmpty();
        if( pt )
            res = formatType(pt->d_return.data());
        else
            res = formatType(member->d_type.data());
        res += " ";
        if( !ma.isEmpty() )
            res += "class ";
        if( record == 0 ) // if module level
            res += moduleRef(member->getModule());
        else
            res += classRef(record);
        res += ma;
        res += "::";
        if( record == 0 ) // if module level
            res += dottedName(member);
        else
            res += escape(member->d_name);
        if( pt )
            res += formatFormals(pt->d_formals);
        forceFormalIndex = false;
        return res;
    }

    QByteArray inline delegateName( const QByteArray& sig )
    {
        QCryptographicHash hash(QCryptographicHash::Md5);
        hash.addData(sig);
        return hash.result().toHex(); // issues because of '/': toBase64(QByteArray::OmitTrailingEquals);
    }

    QByteArray delegateRef( ProcType* pt )
    {
        if( pt == 0 )
            return "?";

        forceAssemblyPrefix = true;

#ifndef _MY_GENERICS_
        const bool old = forceFormalIndex;
        forceFormalIndex = true;
        const QByteArray sig = procTypeSignature(pt);
        forceFormalIndex = old;
#else
        const QByteArray sig = procTypeSignature(pt);
#endif
        const QByteArray name = delegateName(sig);
        if( pt->declaredIn() == thisMod )
            delegates.insert(name,pt);

        Module* m = pt->declaredIn();
        if( m == 0 )
            m = thisMod;
        const QByteArray res = moduleRef(m) + "/'" + name + "'" + formatMetaActuals(pt);
        forceAssemblyPrefix = false;
        return res;
    }

    QByteArray formatArrayCopierRef(Array* a)
    {
        Q_ASSERT(a);
        const QByteArray sig = formatType(a);
        QPair<Array*,int>& d = copiers[sig];
        if( d.first == 0 )
            d.first = a;
        QByteArray res = "void " + moduleRef(thisMod) + "::'#copy'(";
        res += sig;
        res += ", ";
        res += sig;
        res += ")";
        return res;
    }

    void emitArrayCopier( Array* a, const RowCol& loc )
    {
        Q_ASSERT(a);
        Type* et = derefed(a->d_type.data());
        Q_ASSERT(et);

        // the generated procedure is used for both multi- and onedimensional arrays.
        // the multi-dim code is only generated if there is a multi-dim used in code (i.e. dims > 1)
        // the generated procedure assumes array of array if dim > 0 and array of non-array if dim == 0

        emitter->beginMethod("'#copy'", true, IlEmitter::Static );
        const QByteArray type = formatType(a);
        emitter->addArgument(type,"lhs");
        emitter->addArgument(type,"rhs");
        beginBody();

        line(loc); // the same line for the whole method
        const int len = temps.buy();
        Q_ASSERT( len >= 0 );
        emitter->ldarg_(0);
        emitter->ldlen_();
        emitter->ldarg_(1);
        emitter->ldlen_();
        // stack: len lhs, len rhs
        const int lhsIsLen = emitter->newLabel();
        const int storeLen = emitter->newLabel();
        emitter->ble_(lhsIsLen);
        emitter->ldarg_(1);
        emitter->ldlen_();
        emitter->br_(storeLen);
        emitter->label_(lhsIsLen);
        emitter->ldarg_(0);
        emitter->ldlen_();
        emitter->label_(storeLen);
        emitter->stloc_(len); // len = qMin(lenLhs,lenRhs)
        // TODO: only up to and including \0 for char arrays?

        const int idx = temps.buy();
        Q_ASSERT( idx >= 0 );
        emitter->ldc_i4(0);
        emitter->stloc_(idx);

        const int checkLenLbl = emitter->newLabel();
        const int addLbl = emitter->newLabel();
        emitter->label_(checkLenLbl);
        emitter->ldloc_(idx);
        emitter->ldloc_(len);
        const int afterLoopLbl = emitter->newLabel();
        emitter->bge_(afterLoopLbl);

        if( et->getTag() == Thing::T_Array )
        {
            emitter->ldarg_(0);
            emitter->ldloc_(idx);
            // stack: array, int
            emitter->ldelem_(formatType(et));

            emitter->ldarg_(1);
            emitter->ldloc_(idx);
            // stack: array, array, int
            emitter->ldelem_(formatType(et));

            // stack: lhs array, rhs array
            emitter->call_(formatArrayCopierRef(cast<Array*>(et)),2);

            emitter->br_(addLbl);
        }else
        {
            switch( et->getTag() )
            {
            case Thing::T_Record:
                {
                    emitter->ldarg_(0);
                    emitter->ldloc_(idx);
                    // stack: array, int
                    emitter->ldelem_(formatType(et));

                    emitter->ldarg_(1);
                    emitter->ldloc_(idx);
                    // stack: record, array, int
                    emitter->ldelem_(formatType(et));

                    // stack: lhs record, rhs record
                    Record* r2 = cast<Record*>(et);
                    QByteArray type = formatType(r2);
                    if( r2->d_byValue )
                        type += "&";
                    emitter->callvirt_("void class " + classRef(r2) + formatMetaActuals(r2) +
                                        "::'#copy'(" + type + ")", 1 );
               }
                break;
            case Thing::T_Array:
                Q_ASSERT(false); // et always points to the base type of the (multidim) array, which cannot be an array
                break;
            case Thing::T_BaseType:
            case Thing::T_Enumeration:
            case Thing::T_Pointer:
            case Thing::T_ProcType:
                {
                    emitter->ldarg_(0);
                    emitter->ldloc_(idx);
                    // stack: lhs array, int

                    emitter->ldarg_(1);
                    emitter->ldloc_(idx);
                    // stack: lhs array, int, rhs array, int

                    emitter->ldelem_(formatType(et));
                    // stack: lhs array, int, value

                    emitter->stelem_(formatType(et));
                }
                break;
            }
        }

        emitter->label_(addLbl);
        emitter->ldloc_(idx);
        emitter->ldc_i4(1);
        emitter->add_();
        emitter->stloc_(idx);
        emitter->br_(checkLenLbl);
        emitter->label_(afterLoopLbl);
        temps.sell(idx);
        temps.sell(len);

        emitter->ret_();
        emitLocalVars();
        emitter->endMethod();
    }

//#define _USE_VALUE_RECORDS_
    // no value records currently because the initialization works completely different; needs extra work

    void allocRecordDecl(Record* r)
    {
        if( r->d_slotValid )
            return; // can happen e.g. with VAR foo, bar: RECORD ch: CHAR; i: INTEGER END;
        Named* n = r->findDecl();
        if( n == 0 || n->getTag() != Thing::T_NamedType )
        {
            r->d_slot = anonymousDeclNr++;
            r->d_slotValid = true;
        }
    }

    void emitRecordDecl(Record* r)
    {
        if( r->d_slotAllocated )
            return;
        r->d_slotAllocated = true;
        Named* n = r->findDecl();

        QByteArray className, superClassName;
        bool isPublic = false;
        if( n == 0 || n->getTag() != Thing::T_NamedType )
        {
            Q_ASSERT(r->d_slotValid);
            className = "'#" + QByteArray::number(r->d_slot) + "'";
        }else
        {
            isPublic = n->d_scope == thisMod && n->d_visibility == Named::ReadWrite;
#ifdef _USE_VALUE_RECORDS_
            r->d_byValue = !isPublic && r->d_baseRec == 0 && r->d_subRecs.isEmpty();
#else
            r->d_byValue = false;
#endif
            className = dottedName(n);
            if( !r->d_base.isNull() )
                superClassName = formatType(r->d_base.data());
        }
        emitter->beginClass(className, isPublic, superClassName);

        foreach( const Ref<Field>& f, r->d_fields )
            f->accept(this);
        foreach( const Ref<Procedure>& p, r->d_methods )
            p->accept(this);

        // default constructor
        emitter->beginMethod(".ctor",true);
        beginBody();
        line(r->d_loc).ldarg_(0);
        QByteArray what;
        if( r->d_baseRec )
            what = "void class " + classRef(r->d_baseRec) + formatMetaActuals(r->d_baseRec) + "::.ctor()";
        else if( r->d_byValue )
            what = "void [mscorlib]System.ValueType::.ctor()";
        else
            what = "void [mscorlib]System.Object::.ctor()";
        line(r->d_loc).call_(what,1,false,true);

        // initialize fields of current record
        QList<Field*> fields = r->getOrderedFields();
        for( int i = 0; i < fields.size(); i++ )
        {
            // oberon system expects all vars to be initialized
            line(fields[i]->d_loc).ldarg_(0);
            if( emitInitializer(fields[i]->d_type.data(), false, fields[i]->d_loc ) )
                emitStackToVar( fields[i], fields[i]->d_loc );
            else
                line(fields[i]->d_loc).pop_();
        }
        line(r->d_loc).ret_();
        emitLocalVars();
        emitter->endMethod();
        // end default constructor

        // copy
        emitter->beginMethod("'#copy'",true, IlEmitter::Virtual);
        QByteArray type = formatType(r);
        if( r->d_byValue )
            type += "&";
        emitter->addArgument(type, "rhs");
        beginBody();
        if( r->d_baseRec )
        {
            line(r->d_loc).ldarg_(0);
            line(r->d_loc).ldarg_(1);
            QByteArray what = "void class " + classRef(r->d_baseRec) + formatMetaActuals(r->d_baseRec) + "::'#copy'(";
            type = formatType(r->d_baseRec);
            if( r->d_byValue )
                type += "&";
            what += type + ")";
            line(r->d_loc).call_(what,1,false,true);
        }
        for( int i = 0; i < fields.size(); i++ )
        {
            Type* ft = derefed(fields[i]->d_type.data());
            switch( ft->getTag() )
            {
            case Thing::T_Record:
                {
                    line(r->d_loc).ldarg_(0);
                    line(r->d_loc).ldfld_(memberRef(fields[i]));
                    line(r->d_loc).ldarg_(1);
                    line(r->d_loc).ldfld_(memberRef(fields[i]));
                    Record* r2 = cast<Record*>(ft);
                    QByteArray what = "void class " + classRef(r2) + formatMetaActuals(r2) + "::'#copy'(";
                    type = formatType(r2);
                    if( r2->d_byValue )
                        type += "&";
                    what += type + ")";
                    line(r->d_loc).callvirt_(what,1);
                }
                break;
            case Thing::T_Array:
                {
                    line(r->d_loc).ldarg_(0);
                    line(r->d_loc).ldfld_(memberRef(fields[i]));

                    line(r->d_loc).ldarg_(1);
                    line(r->d_loc).ldfld_(memberRef(fields[i]));

                    // stack: lhs array, rhs array
                    line(r->d_loc).call_(formatArrayCopierRef(cast<Array*>(ft)),2);
                }
                break;
            case Thing::T_BaseType:
            case Thing::T_Enumeration:
            case Thing::T_Pointer:
            case Thing::T_ProcType:
                line(r->d_loc).ldarg_(0);
                line(r->d_loc).ldarg_(1);
                line(r->d_loc).ldfld_(memberRef(fields[i]));
                line(r->d_loc).stfld_(memberRef(fields[i]));
                break;
            }
        }
        line(r->d_loc).ret_();
        emitLocalVars();
        emitter->endMethod();
        // end copy

        emitter->endClass();
    }

    void emitDelegDecl(ProcType* sig, const QByteArray& name)
    {
        // NOTE: if the name deviates from the one used for referencing the delegate mono3 crashes with this message:
        // TypeRef ResolutionScope not yet handled (3) for .48b15Qezth5ae11+xOqLVw in image GenericTest6.dll
        // * Assertion at class.c:5695, condition `!mono_loader_get_last_error ()' not met

        emitter->beginClass(escape(name),true,"[mscorlib]System.MulticastDelegate"); // sealed
        // formatMetaParams(thisMod)
        emitter->beginMethod(".ctor",true,IlEmitter::Instance,true);
        emitter->addArgument("object","MethodsClass");
        emitter->addArgument("native unsigned int", "MethodPtr");
        emitter->endMethod();
        emitter->beginMethod("Invoke",true,IlEmitter::Instance,true);
        if( !sig->d_return.isNull() )
            emitter->setReturnType(formatType(sig->d_return.data()));
        for( int i = 0; i < sig->d_formals.size(); i++ )
        {
            QByteArray type = formatType(sig->d_formals[i]->d_type.data());
            if( passByRef(sig->d_formals[i].data()) )
                type += "&";

            emitter->addArgument(type,escape(sig->d_formals[i]->d_name));
        }
        emitter->endMethod();
        emitter->endClass();
    }

    QByteArray formatMetaParams(Module* m)
    {
#ifdef _MY_GENERICS_
        return QByteArray();
#else
        if( m->d_metaParams.isEmpty() )
            return QByteArray();
        QByteArray res = "<";
        for( int i = 0; i < m->d_metaParams.size(); i++ )
        {
            if( i != 0 )
                res += ", ";
            res += escape(m->d_metaParams[i]->d_name);
        }
        res += ">";
        return res;
#endif
    }

    void visit( Module* me )
    {
        ObxCilGenCollector co;
        me->accept(&co);

        foreach( Import* imp, me->d_imports )
        {
            if(imp->d_mod->d_synthetic || imp->d_mod->d_isDef ) // TODO: def
                continue; // ignore SYSTEM
            co.allImports.insert(imp->d_mod.data());
            if( !imp->d_mod.isNull() && !imp->d_mod->d_metaActuals.isEmpty() )
            {
                for( int i = 0; i < imp->d_mod->d_metaActuals.size(); i++ )
                {
                    Type* at = imp->d_mod->d_metaActuals[i].data();
                    //Q_ASSERT( !at->d_slotValid );
                    at->d_slot = i;
                    at->d_slotValid = true;
                    at->d_metaActual = true;
                }
            }
        }
        QByteArrayList imports;
        imports.append( escape("mscorlib") );
        imports.append( escape("OBX.Runtime") );
        foreach( Module* m, co.allImports )
        {
            if( m && m != me )
                imports.append( escape(getName(m)) );
        }

        // NOTE: module name is always set in '' and thus doesn't have to be escaped
        emitter->beginModule(escape(me->getName()),imports, thisMod->d_file);

        for( int i = 0; i < co.allProcTypes.size(); i++ )
            delegateRef(co.allProcTypes[i]);

        foreach( Record* r, co.allRecords )
            allocRecordDecl(r);

        foreach( Record* r, co.allRecords )
            emitRecordDecl(r);

        foreach( const Ref<Named>& n, me->d_order )
        {
            if( n->getTag() == Thing::T_Variable )
                n->accept(this);
        }
#ifndef _MY_GENERICS_
        if( !me->d_metaParams.isEmpty() && me->d_metaActuals.isEmpty() )
        {
            foreach( const Ref<GenericName>& n, me->d_metaParams )
            {
                Q_ASSERT( n->d_slotValid );
                out << ws() << ".field assembly static !" << n->d_slot << " '##" << n->d_slot << "'" << endl;
            }
        }
#endif

        foreach( Procedure* p, co.allProcs )
            p->accept(this);

        emitter->beginMethod(".cctor", false, IlEmitter::Static ); // MODULE BEGIN
        beginBody();
#ifndef _MY_GENERICS_
        if( !me->d_metaParams.isEmpty() && me->d_metaActuals.isEmpty() )
        {
            foreach( const Ref<GenericName>& n, me->d_metaParams ) // generate default values
            {
                // NOTE: this doesn't initialize OBX value types; e.g. in GenericTest3 l1.value is initialized to null instead
                // of an empty array 20 of char; to get around a default constructor for all possible types, especially
                // fixed size arrays, would be needed.
                emitOpcode2("ldsflda ",  1, me->d_begin );
                Q_ASSERT(n->d_slotValid);
                out << "!" << QByteArray::number(n->d_slot) << " class " << moduleRef(me) << formatMetaActuals(me)
                    << "::'##" << n->d_slot << "'" << endl;
                emitOpcode("initobj !"+escape(n->d_name),-1, me->d_begin);
            }
        }
#endif

        foreach( const Ref<Named>& n, me->d_order )
        {
            if( n->getTag() == Thing::T_Variable )
                emitInitializer(n.data());
        }
        foreach( const Ref<Statement>& s, me->d_body )
            s->accept(this);

#if 0  // TEST
        foreach( Import* imp, me->d_imports )
        {
            if( imp->d_mod->d_synthetic || imp->d_mod->d_isDef )
                continue;
            emitOpcode("call void class ['" + getName(imp->d_mod.data()) + "']'" +
                       getName(imp->d_mod.data()) + "'" + formatMetaActuals(imp->d_mod.data()) + "::'ping#'()",0, me->d_begin );
        }
#if 0
        emitOpcode( "ldstr \"this is " + me->getName() + "\"", 1, me->d_begin );
        emitOpcode( "call void [mscorlib]System.Console::WriteLine (string)", -1, me->d_begin );
#endif
#endif
        line(me->d_begin).ret_(false);

        emitLocalVars();

        emitter->endMethod();

#if 1 // TEST
        emitter->beginMethod("'ping#'", true, IlEmitter::Static ); // NOP, just to wakeup the assembly
        line(me->d_end).ret_();
        emitter->endMethod();
#endif

        QSet<QByteArray> done;
        while( !copiers.isEmpty() )
        {
            QByteArray t = copiers.begin().key();
            //const int dims = copiers.begin().value().second;
            Array* a = copiers.begin().value().first;
            copiers.remove(t);
            if( done.contains(t) )
                continue;
            emitArrayCopier(a, me->d_end );
            done.insert(t);
        }

        QHash<QByteArray,ProcType*>::const_iterator i;
        for( i = delegates.begin(); i != delegates.end(); ++i )
            emitDelegDecl( i.value(), i.key() );

        emitter->endModule();
    }

    QByteArray procTypeSignature(ProcType* pt)
    {
        QByteArray str;
        if( pt->d_return.isNull() )
            str = "void";
        else
            str = formatType(pt->d_return.data());
        str += "*";
        str += formatFormals(pt->d_formals,false);
        return str;
    }

    QByteArray formatType( Type* t )
    {
        if( t == 0 )
            return "void";
        else if( forceFormalIndex && t->d_metaActual )
            return "!"+QByteArray::number(t->d_slot);
        switch(t->getTag())
        {
        case Thing::T_Array:
            {
                Array* me = cast<Array*>(t);
                // we only support CLI vectors; multi dim are vectors of vectors
                // arrays are constructed types, i.e. all qualis are resolved up to their original module
                // arrays are always dynamic in CLI; the size of an array is an attribute of the instance
                if( me->d_type )
                    return formatType(me->d_type.data()) + "[]";
            }
            break;
        case Thing::T_BaseType:
            return formatBaseType(t->getBaseType());
        case Thing::T_Enumeration:
            return "uint16";
        case Thing::T_Pointer:
            {
                Pointer* me = cast<Pointer*>(t);
                // this is a CLI object reference; since all objects and arrays in CLI are dynamic,
                // a field of type object or array is always a pointer, whereas implicit;
                if( me->d_to )
                    return formatType(me->d_to.data());
            }
            break;
        case Thing::T_ProcType:
            {
                ProcType* pt = cast<ProcType*>(t);
                return "class " + delegateRef(pt);
            }
            break;
        case Thing::T_QualiType:
            {
                QualiType* me = cast<QualiType*>(t);
                Type* td = derefed(me->d_quali->d_type.data());
                if( td == 0 )
                    break; // error already reported
                // all qualis are immediatedly resolved

                if( me->d_selfRef )
                {
                    if( Record* r = td->toRecord() )
                        return formatType(r);
                    else
                        return "[mscorlib]System.Object"; // avoid infinite loop
                }
#ifndef _MY_GENERICS_
                else if( td->getBaseType() == Type::ANY )
                {
                    Named* n = me->d_quali->getIdent();
                    Q_ASSERT( n && n->getTag() == Thing::T_GenericName );
                    Q_ASSERT( n->d_slotValid );
                    return "!"+QByteArray::number(n->d_slot);
                }
#endif
                else
                    return formatType(me->d_quali->d_type.data());
            }
            break;
        case Thing::T_Record:
            return "class " + classRef(cast<Record*>(t))
                    + formatMetaActuals(t)
                    ;
        default:
            Q_ASSERT(false);
        }
        return "?";
    }

    void visit( ProcType* me )
    {
        Q_ASSERT(false);
    }

    void visit( Record* me)
    {
        Q_ASSERT(false);
    }

    void visit( Enumeration* me)
    {
        Q_ASSERT(false);
    }

    void visit( QualiType* me)
    {
        Q_ASSERT(false);
    }

    void visit( Array* me)
    {
        Q_ASSERT(false);
    }

    void visit( Pointer* me)
    {
        Q_ASSERT(false);
    }

    static inline QByteArray formatBaseType(int t)
    {
        switch( t )
        {
        case Type::BOOLEAN:
            return "bool";
        case Type::CHAR:
        case Type::WCHAR:
            return "char";
        case Type::BYTE:
            return "uint8";
        case Type::SHORTINT:
            return "int16";
        case Type::INTEGER:
            return "int32";
        case Type::LONGINT:
            return "int64";
        case Type::REAL:
            return "float32";
        case Type::LONGREAL:
            return "float64";
        case Type::SET:
            return "int32";
        default:
            return "?";
        }
    }

    void visit( BaseType* me)
    {
        Q_ASSERT(false);
    }

    void emitVar( Named* me, bool isStatic )
    {
        emitter->addField(escape(me->d_name),formatType(me->d_type.data()),
                         me->d_visibility == Named::ReadWrite || me->d_visibility == Named::ReadOnly, isStatic );
    }

    void visit( Variable* me)
    {
        emitVar(me,true);
        // initializer is emitted in module .cctor
    }

    void visit( Field* me )
    {
        emitVar(me,false);
    }

    QByteArray formatFormals( const ProcType::Formals& formals, bool withName = true )
    {
        QByteArray res = "(";
        for( int i = 0; i < formals.size(); i++ )
        {
            if( i != 0 )
                res += ", ";
            res += formatType( formals[i]->d_type.data() );
            if( passByRef(formals[i].data()) )
                res += "&";
            if( withName )
                res += " " + escape(formals[i]->d_name);
        }
        res += ")";
        return res;
    }

    void emitLocalVars()
    {
        for( int i = 0; i <= temps.d_max; i++ )
            emitter->addLocal( "int32", escape("#temp" + QByteArray::number(i) ) ); // before was natural int
    }

    void visit( Procedure* me )
    {
        Q_ASSERT( scope == 0 );
        scope = me;

        QByteArray name;
        if( me->d_receiverRec )
            name = escape(me->d_name);
        else
            name = dottedName(me);

        IlEmitter::MethodKind k;
        if( me->d_receiver.isNull() )
            k = IlEmitter::Static;
        else if( me->d_receiverRec && !me->d_receiverRec->d_byValue )
            k = IlEmitter::Virtual;
        else
            k = IlEmitter::Instance;

        emitter->beginMethod(name,me->d_visibility != Named::Private,k);

        ProcType* pt = me->getProcType();
        if( !pt->d_return.isNull() )
            emitter->setReturnType(formatType(pt->d_return.data()));

        // allocate params and local
        int off = me->d_receiver.isNull() ? 0 : 1;
        for( int i = 0; i < pt->d_formals.size(); i++ )
        {
            pt->d_formals[i]->d_slot = i+off; // for type-bounds arg0 is self
            pt->d_formals[i]->d_slotValid = true;

            QByteArray type = formatType(pt->d_formals[i]->d_type.data());
            if( passByRef(pt->d_formals[i].data()) )
                type += "&";
            emitter->addArgument(type,escape(pt->d_formals[i]->d_name));
        }
        off = 0;
        foreach( const Ref<Named>& n, me->d_order )
        {
            if( n->getTag() == Thing::T_LocalVar )
            {
                n->d_slot = off++;
                n->d_slotValid = true;
                emitter->addLocal( formatType(n->d_type.data()), escape(n->d_name) );
            }
        }

        beginBody(me->d_varCount);

        foreach( const Ref<Named>& n, me->d_order )
        {
            switch( n->getTag() )
            {
            case Thing::T_LocalVar:
            case Thing::T_Parameter:
                emitInitializer(n.data());
                break;
            }
        }
        foreach( const Ref<Statement>& s, me->d_body )
            s->accept(this);
        if( me->d_body.isEmpty() || me->d_body.last()->getTag() != Thing::T_Return )
            emitReturn( pt, 0, me->d_end );

        emitLocalVars();

        emitter->endMethod();
        scope = 0;
    }

    void beginBody(quint16 start = 0)
    {
        last = RowCol();
        temps.reset(start);
    }

    void visit( LocalVar* me )
    {
        Q_ASSERT(false);
    }

    void emitConst(quint8 basetype, const QVariant& val, const RowCol& loc )
    {
        switch( basetype )
        {
        case Type::BOOLEAN:
            if( val.toBool() )
                line(loc).ldc_i4(1);
            else
                line(loc).ldc_i4(0);
            break;
        case Type::SHORTINT:
            line(loc).ldc_i4(val.toInt());
            break;
        case Type::INTEGER:
            line(loc).ldc_i4(val.toInt());
            break;
        case Type::LONGINT:
            line(loc).ldc_i8(val.toInt());
            break;
        case Type::BYTE:
            line(loc).ldc_i4(val.toInt());
            break;
        case Type::ENUMINT:
            line(loc).ldc_i4(val.toInt());
            break;
        case Type::REAL:
            //emitOpcode2("ldc.r8",1,loc); // NOTE: before r4, but this causes round-off errors when e.g. 365.24 is later converted to r8
                                         // CLR anyway has F on the stack, even when pushing r4
            //out << " " << QByteArray::number(val.toDouble(),'e',9) << endl;
            line(loc).ldc_r8(val.toDouble());
            break;
        case Type::LONGREAL:
            //emitOpcode2("ldc.r8",1,loc);
            //out << " " << QByteArray::number(val.toDouble(),'e',17) << endl;
            line(loc).ldc_r8(val.toDouble());
            break;
        case Type::NIL:
            line(loc).ldnull_();
            break;
        case Type::STRING:
        case Type::WSTRING:
            {
                QByteArray str = val.toByteArray();
                str.replace('\\', "\\\\");
                str.replace("\"","\\\"");
                line(loc).ldstr_("\"" + str + "\\0" + "\""); // without explicit \0 the resulting char[] has no trailing zero!
                line(loc).callvirt_("char[] [mscorlib]System.String::ToCharArray()",0,true);
#if 0 // TEST
                line(loc).dup_();
                line(loc).ldlen_();
                line(loc).call_("void [mscorlib]System.Console::WriteLine(int32)",1);
                line(loc).dup_();
                line(loc).call_("int32 [OBX.Runtime]OBX.Runtime::strlen(char[])",1,true);
                line(loc).call_("void [mscorlib]System.Console::WriteLine(int32)",1);
#endif
            }
            break;
        case Type::BYTEARRAY:
            {
                const QByteArray ba = val.toByteArray();
                line(loc).ldc_i4(ba.size());
                line(loc).newarr_("uint8");

                for( int i = 0; i < ba.size(); i++ ) // TODO: this is inefficient
                {
                    line(loc).dup_();
                    line(loc).ldc_i4(i);
                    line(loc).ldc_i4((quint8)ba[i]);
                    line(loc).stelem_("uint8");
                }
            }
            break;
        case Type::CHAR:
        case Type::WCHAR:
            line(loc).ldc_i4(val.toUInt());
            break;
        case Type::SET:
            {
                Literal::SET s = val.value<Literal::SET>();
                line(loc).ldc_i4(s.to_ulong());
            }
            break;
        default:
            Q_ASSERT(false);
        }
    }

    void visit( Literal* me)
    {
        Type* td = derefed(me->d_type.data());
        Q_ASSERT( td && ( td->getTag() == Thing::T_BaseType || td->getTag() == Thing::T_Enumeration) );
        // Enumeration has basetype ENUMINT
        emitConst( td->getBaseType(), me->d_val, me->d_loc );
    }

    void visit( UnExpr* me)
    {
        Q_ASSERT( !me->d_sub.isNull() );

        me->d_sub->accept(this);

        // prev must be a pointer or a record
        Type* prevT = derefed(me->d_sub->d_type.data());
        Q_ASSERT( prevT );

        switch( me->d_op )
        {
        case UnExpr::NEG:
            if( prevT->getBaseType() == BaseType::SET )
            {
                line(me->d_loc).not_();
            }else
            {
                Q_ASSERT( prevT->isNumeric() );
                line(me->d_loc).neg_();
            }
            return;
        case UnExpr::NOT:
            line(me->d_loc).ldc_i4(0);
            line(me->d_loc).ceq_();
            return;
        case UnExpr::DEREF:
            // NOP: both pointer deref as well as super proc calls are handled by referencing UnExpr
            return;
        case UnExpr::ADDROF:
            // NOP
            return;
        default:
            qDebug() << "ERR" << me->d_op << thisMod->d_name << me->d_loc.d_row << me->d_loc.d_col;
            Q_ASSERT( false );
            break;
        }
        Q_ASSERT( false );
    }

    void visit( IdentLeaf* me)
    {
        Named* id = me->getIdent();
        if( id == 0 )
            return; // already reported

        switch( id->getTag() )
        {
        case Thing::T_Const:
            {
                Type* td = derefed(me->d_type.data() );
                Q_ASSERT( td && ( td->getTag() == Thing::T_BaseType || td->getTag() == Thing::T_Enumeration ) );
                emitConst( td->getBaseType(), cast<Const*>(id)->d_val, me->d_loc );
            }
            return;
        case Thing::T_Import:
            // NOP
            return;
        case Thing::T_Variable:
        case Thing::T_LocalVar:
            emitVarToStack(id,me->d_loc);
            return;
        case Thing::T_Parameter:
            {
                Parameter* p = cast<Parameter*>(id);
                emitVarToStack(id,me->d_loc);
                if( passByRef(p) ) // the value on the stack is a &, so we need to fetch the value first
                {
#ifdef _USE_LDSTOBJ
                    line(me->d_loc).ldobj_(formatType(p->d_type.data()));
#else
                    Type* td = derefed(p->d_type.data());
                    switch( td->getTag() )
                    {
                    case Thing::T_Array:
                    case Thing::T_Record:
                    default:
                        Q_ASSERT( false ); // never happens because no passByRef for structured types
                        break;
                    case Thing::T_Pointer:
                    case Thing::T_ProcType:
                        emitOpcode("ldind.ref", -1+1, me->d_loc);
                        break;
                    case Thing::T_Enumeration:
                        emitOpcode("ldind.u4", -1+1, me->d_loc);
                        break;
                    case Thing::T_BaseType:
                        switch(td->getBaseType())
                        {
                        case Type::LONGREAL:
                            emitOpcode("ldind.r8",-1+1, me->d_loc);
                            break;
                        case Type::REAL:
                            emitOpcode("ldind.r4",-1+1, me->d_loc);
                            break;
                        case Type::LONGINT:
                            emitOpcode("ldind.i8",-1+1, me->d_loc);
                            break;
                        case Type::INTEGER:
                            emitOpcode("ldind.i4",-1+1, me->d_loc);
                            break;
                        case Type::SET:
                            emitOpcode("ldind.u4",-1+1, me->d_loc);
                            break;
                        case Type::SHORTINT:
                            emitOpcode("ldind.i2",-1+1, me->d_loc);
                            break;
                        case Type::CHAR:
                        case Type::WCHAR:
                            emitOpcode("ldind.u2",-1+1, me->d_loc);
                            break;
                        case Type::BYTE:
                        case Type::BOOLEAN: // bool is 1 byte in RAM but 4 bytes on stack
                            emitOpcode("ldind.u1",-1+1, me->d_loc);
                            break;
                        }
                        break;
                    }
#endif
                }
            }
            return;
        case Thing::T_NamedType:
            // NOP
            return;
        case Thing::T_BuiltIn:
        case Thing::T_Procedure:
            // NOP
            return;
        default:
            qDebug() << "ERR" << id->getTag() << thisMod->d_name << me->d_loc.d_row << me->d_loc.d_col;
            Q_ASSERT( false );
            break;
        }
        Q_ASSERT( false );
    }

    void visit( IdentSel* me)
    {
        Q_ASSERT( !me->d_sub.isNull() );

        Named* subId = me->d_sub->getIdent();
        const bool derefImport = subId && subId->getTag() == Thing::T_Import;

        me->d_sub->accept(this);

        Named* id = me->getIdent();
        Q_ASSERT( id );

        switch( id->getTag() )
        {
        case Thing::T_Procedure:
            // NOP
            return;
        case Thing::T_Field:
            Q_ASSERT( me->d_sub && me->d_sub->d_type->toRecord() );
            emitVarToStack(id, me->d_loc);
            return;
        case Thing::T_Variable:
            Q_ASSERT( derefImport );
            emitVarToStack(id, me->d_loc);
            return;
        case Thing::T_NamedType:
            // NOP
            return;
        case Thing::T_Const:
            {
                Q_ASSERT( derefImport );
                Type* td = derefed(id->d_type.data() );
                Q_ASSERT( td && ( td->getTag() == Thing::T_BaseType || td->getTag() == Thing::T_Enumeration ) );
                emitConst( td->getBaseType(), cast<Const*>(id)->d_val, me->d_loc );
            }
            return;
        case Thing::T_BuiltIn:
            // NOP
            return;
        default:
            qDebug() << "ERR" << thisMod->d_name << id->getTag() << me->d_loc.d_row << me->d_loc.d_col;
            Q_ASSERT( false );
            break;
        }
        Q_ASSERT( false );
    }

    void emitIndex( ArgExpr* me )
    {
        Q_ASSERT( me->d_sub );
        me->d_sub->accept(this);
        Type* subT = derefed(me->d_sub->d_type.data());
        Q_ASSERT( subT && subT->getTag() == Thing::T_Array);

        Q_ASSERT( me->d_args.size() == 1 );
        me->d_args.first()->accept(this);

        Type* et = derefed(cast<Array*>(subT)->d_type.data());
        if( et == 0 )
            return; // already reported
        line(me->d_loc).ldelem_(formatType(et));
    }

    void emitFetchDesigAddr(Expression* desig, bool omitParams = true )
    {
        const int unop = desig->getUnOp();
        const int tag = desig->getTag();
        if( unop == UnExpr::SEL )
        {
            Q_ASSERT( desig->getTag() == Thing::T_IdentSel );
            IdentSel* sel = cast<IdentSel*>(desig);
            Named* id = sel->getIdent();
            Q_ASSERT( id );
            sel->d_sub->accept(this);
            switch( id->getTag() )
            {
            case Thing::T_Variable:
                line(desig->d_loc).ldsflda_(memberRef(id));
                break;
            case Thing::T_Field:
                line(desig->d_loc).ldflda_(memberRef(id));
                break;
            default:
                Q_ASSERT( false );
           }
        }else if( unop == UnExpr::IDX )
        {
            Q_ASSERT( desig->getTag() == Thing::T_ArgExpr );
            ArgExpr* args = cast<ArgExpr*>( desig );
            Q_ASSERT( args->d_args.size() == 1 );
            args->d_sub->accept(this); // stack: array
            args->d_args.first()->accept(this); // stack: array, index
            line(desig->d_loc).ldelema_(formatType(desig->d_type.data()));
        }else if( unop == UnExpr::CAST )
        {
            Q_ASSERT( desig->getTag() == Thing::T_ArgExpr );
            ArgExpr* args = cast<ArgExpr*>( desig );
            emitFetchDesigAddr( args->d_sub.data(), omitParams );
        }else if( unop == UnExpr::DEREF )
        {
            Q_ASSERT( desig->getTag() == Thing::T_UnExpr );
            UnExpr* ue = cast<UnExpr*>( desig );
            emitFetchDesigAddr( ue->d_sub.data(), omitParams );
        }else if( tag == Thing::T_IdentLeaf )
        {
            Named* n = desig->getIdent();
            switch( n->getTag() )
            {
            case Thing::T_Variable:
                line(desig->d_loc).ldsflda_(memberRef(n));
                break;
            case Thing::T_Parameter:
                Q_ASSERT( n->d_slotValid );
                if( omitParams && passByRef(cast<Parameter*>(n)) )
                    line(desig->d_loc).ldarg_(n->d_slot); // we already have the address of the value
                else
                    line(desig->d_loc).ldarga_(n->d_slot);
                break;
            case Thing::T_LocalVar:
                Q_ASSERT( n->d_slotValid );
                line(desig->d_loc).ldloca_(n->d_slot);
                // NOTE: works only for local access
                break;
            }
        }else if( tag == Thing::T_Literal )
        {
            Q_ASSERT( cast<Literal*>(desig)->d_vtype == Literal::Nil );
            // this happens in BB when calling the Win32 API
            line(desig->d_loc).ldnull_();
        }else if( tag == Thing::T_ArgExpr )
        {
            ArgExpr* ae = cast<ArgExpr*>(desig);
            Q_ASSERT( ae->d_sub && ae->d_sub->getIdent() && ae->d_sub->getIdent()->getTag() == Thing::T_BuiltIn &&
                     ( cast<BuiltIn*>(ae->d_sub->getIdent())->d_func == BuiltIn::SYS_VAL ||
                       cast<BuiltIn*>(ae->d_sub->getIdent())->d_func == BuiltIn::VAL ) );
            Q_ASSERT( ae->d_args.size() == 2 );
            emitFetchDesigAddr(ae->d_args.last().data(), omitParams);
        }else
        {
            qDebug() << "ERR" << desig->getUnOp() << desig->getTag() << thisMod->getName() << desig->d_loc.d_row << desig->d_loc.d_col;
            Q_ASSERT( false );
        }
    }

    void emitBuiltIn( BuiltIn* bi, ArgExpr* ae )
    {
        switch( bi->d_func )
        {
        case BuiltIn::PRINTLN:
            {
                Q_ASSERT( ae->d_args.size() == 1 );
                ae->d_args.first()->accept(this);
                Type* t = derefed(ae->d_args.first()->d_type.data());
                if( t->isText() )
                {
                    if( t->isChar() )
                        line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(char)",1);
                    else
                        line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(char[])",1);
                }else if( t->isInteger() )
                {
                    if( t->getBaseType() <= Type::INTEGER )
                        line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(int32)",1);
                    else
                        line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(int64)",1);
                }else if( t->isReal() )
                    line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(float64)",1);
                else if( t->isSet() )
                    line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(uint32)",1);
                else if( t->getBaseType() == Type::BOOLEAN )
                    line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(bool)",1);
                else
                {
                    switch(t->getTag())
                    {
                    case Thing::T_Enumeration:
                        line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(uint32)",1);
                        break;
                    default:
                        line(ae->d_loc).call_("void [mscorlib]System.Console::WriteLine(object)",1);
                    }
                }
            }
            break;
        case BuiltIn::INC:
        case BuiltIn::DEC:
            {
                Ref<BinExpr> add = new BinExpr();
                add->d_lhs = ae->d_args.first();
                add->d_type = ae->d_args.first()->d_type;
                add->d_loc = ae->d_args.first()->d_loc;
                if( bi->d_func == BuiltIn::INC )
                    add->d_op = BinExpr::ADD;
                else
                    add->d_op = BinExpr::SUB;
                if( ae->d_args.size() == 1 )
                {
                    add->d_rhs = new Literal( Literal::Integer,add->d_loc,qlonglong(1));
                    add->d_rhs->d_type = ae->d_args.first()->d_type;
                }else
                {
                    Q_ASSERT( ae->d_args.size() == 2 );
                    add->d_rhs = ae->d_args.last();
                }
                Ref<Assign> ass = new Assign();
                ass->d_lhs = ae->d_args.first();
                ass->d_loc = ae->d_loc;
                ass->d_rhs = add.data();
                ass->accept(this);
            }
            break;
        case BuiltIn::TRAP:
            // doesn't work:
            //emitOpcode("ldstr \"trap hit\"",1,ae->d_loc); // TEST
            //emitOpcode("call void [mscorlib]System.Console::WriteLine(string)",-1,ae->d_loc);
            line(ae->d_loc).break_();
            break;
        case BuiltIn::TRAPIF:
            {
                Q_ASSERT( ae->d_args.size() == 1 );
                ae->d_args.first()->accept(this);
                const int atEnd = emitter->newLabel();
                line(ae->d_loc).brfalse_(atEnd);
                line(ae->d_loc).break_();
                line(ae->d_loc).label_(atEnd);
            }
            break;
        case BuiltIn::MAX:
        case BuiltIn::MIN:
            if( ae->d_args.size() == 1 )
            {
                Type* t = derefed(ae->d_args.first()->d_type.data());
                switch( t->getTag() )
                {
                case Thing::T_BaseType:
                    {
                        BaseType* bt = cast<BaseType*>(t);
                        switch( bt->getBaseType() )
                        {
                        case Type::LONGINT:
                            if( bi->d_func == BuiltIn::MAX )
                                line(ae->d_loc).ldc_i8(bt->maxVal().toLongLong());
                            else
                                line(ae->d_loc).ldc_i8(bt->minVal().toLongLong());
                            break;
                        case Type::LONGREAL:
                            if( bi->d_func == BuiltIn::MAX )
                                line(ae->d_loc).ldc_r8(bt->maxVal().toDouble());
                            else
                                line(ae->d_loc).ldc_r8(bt->minVal().toDouble());
                            break;
                        case Type::REAL:
                            if( bi->d_func == BuiltIn::MAX ) // NOTE: used r4 before, but see above
                                line(ae->d_loc).ldc_r8(bt->maxVal().toDouble());
                            else
                                line(ae->d_loc).ldc_r8(bt->minVal().toDouble());
                            break;
                        case Type::BOOLEAN:
                        case Type::CHAR:
                        case Type::WCHAR:
                        case Type::BYTE:
                        case Type::SHORTINT:
                        case Type::INTEGER:
                        case Type::SET:
                            if( bi->d_func == BuiltIn::MAX )
                                line(ae->d_loc).ldc_i4(bt->maxVal().toInt());
                            else
                                line(ae->d_loc).ldc_i4(bt->minVal().toInt());
                            break;
                        }
                    }
                    break;
                case Thing::T_Enumeration:
                    {
                        Enumeration* e = cast<Enumeration*>(t);
                        if( bi->d_func == BuiltIn::MAX )
                            line(ae->d_loc).ldc_i4(e->d_items.last()->d_val.toInt());
                        else
                            line(ae->d_loc).ldc_i4(e->d_items.first()->d_val.toInt());
                    }
                    break;
                default:
                    Q_ASSERT( false );
                }
            }else if( ae->d_args.size() == 2 )
            {
                ae->d_args.first()->accept(this);
                ae->d_args.last()->accept(this);
                const int posCase = emitter->newLabel();
                if( bi->d_func == BuiltIn::MAX )
                    line(ae->d_loc).bge_(posCase);
                else
                    line(ae->d_loc).ble_(posCase); // if
                ae->d_args.last()->accept(this); // then

                const int toEnd = emitter->newLabel();
                line(ae->d_loc).br_(toEnd);
                line(ae->d_loc).label_(posCase);
                ae->d_args.first()->accept(this); // else
                line(ae->d_loc).label_(toEnd);
                // TODO stackDepth--; // correct for alternative
            }else
                Q_ASSERT( false );
            break;
        case BuiltIn::DEFAULT:
            {
                Q_ASSERT( !ae->d_args.isEmpty() && !ae->d_args.first()->d_type.isNull() );
                Expression* e = ae->d_args.first().data();
                if( !emitInitializer(e->d_type.data(),false,e->d_loc) )
                    line(ae->d_loc).ldnull_();
            }
            break;
        case BuiltIn::LEN:
            {
                // TODO: len with two args
                Q_ASSERT( !ae->d_args.isEmpty() );
                Type* t = derefed(ae->d_args.first()->d_type.data() );
                if( t && t->getTag() == Thing::T_Pointer )
                    t = derefed( cast<Pointer*>(t)->d_to.data() );

                if( t->isString() )
                {
                    ae->d_args.first()->accept(this);
                    line(ae->d_loc).call_("int32 [OBX.Runtime]OBX.Runtime::strlen(char[])",1,true);
                }else
                {
                    Q_ASSERT( t->getTag() == Thing::T_Array );
                    Array* a = cast<Array*>(t);
                    Type* at = derefed( a->d_type.data() );
                    Q_ASSERT( at );
                    if( a->d_len > 0 )
                    {
                        line(ae->d_loc).ldc_i4(a->d_len);
                    }else
                    {
                        ae->d_args.first()->accept(this);
                        line(ae->d_loc).ldlen_();
                    }
                }
            }
            break;
        case BuiltIn::NEW:
            {
                Q_ASSERT( !ae->d_args.isEmpty() );

                Type* t = ae->d_args.first()->d_type.data();
                Type* td = derefed(t);
                Q_ASSERT( td && td->getTag() == Thing::T_Pointer );
                //Pointer* ptr = cast<Pointer*>(td);

                QList<int> lengths;
                for( int i = 1; i < ae->d_args.size(); i++ )
                {
                    ae->d_args[i]->accept(this);
                    const int len = temps.buy();
                    lengths.append(len);
                    line(ae->d_loc).stloc_(len);
                }

                emitFetchDesigAddr(ae->d_args.first().data(),true); // not false, because also here a var param has the address already
                // stack: address to store to

                // we must pass t here (not ptr->d_to) because the pointer could be a named type defined in another module;
                // if we deref the pointer we lose the module information
                emitInitializer(t, true, ae->d_loc, lengths );

                line(ae->d_loc).stind_(IlEmitter::Ref);
            }
            break;
        case BuiltIn::INCL:
            {
                Q_ASSERT( ae->d_args.size() == 2 );

                emitFetchDesigAddr(ae->d_args.first().data(),true);
                // stack: addr of store
                line(ae->d_loc).dup_();
                line(ae->d_loc).ldind_(IlEmitter::U4);
                ae->d_args.last()->accept(this);
                line(ae->d_loc).call_("int32 [OBX.Runtime]OBX.Runtime::addElemToSet(int32,int32)",2,true );
                line(ae->d_loc).stind_(IlEmitter::I4);
            }
            break;
        case BuiltIn::EXCL:
            {
                Q_ASSERT( ae->d_args.size() == 2 );

                emitFetchDesigAddr(ae->d_args.first().data(),true);
                // stack: addr of store
                line(ae->d_loc).dup_();
                line(ae->d_loc).ldind_(IlEmitter::U4);
                ae->d_args.last()->accept(this);
                line(ae->d_loc).call_("int32 [OBX.Runtime]OBX.Runtime::removeElemFromSet(int32,int32)", 2, true );
                line(ae->d_loc).stind_(IlEmitter::I4);
            }
            break;
        case BuiltIn::PACK:
            {
                Q_ASSERT( ae->d_args.size() == 2 );

                emitFetchDesigAddr(ae->d_args.first().data(),true);
                ae->d_args.last()->accept(this);
                line(ae->d_loc).call_("void [OBX.Runtime]OBX.Runtime::PACK(float32&, int32)", 2 );
           }
            break;
        case BuiltIn::UNPK:
            {
                Q_ASSERT( ae->d_args.size() == 2 );

                emitFetchDesigAddr(ae->d_args.first().data(),true);
                emitFetchDesigAddr(ae->d_args.last().data(),true);
                // stack: addr, addr
                line(ae->d_loc).call_("void [OBX.Runtime]OBX.Runtime::UNPACK(float32&, int32&)", 2 );
             }
            break;
        case BuiltIn::ORD:
            {
                Q_ASSERT( ae->d_args.size() == 1 );
                ae->d_args.first()->accept(this);
                Type* t = derefed(ae->d_args.first()->d_type.data() );
                if( t && ( t->isString() || t->isStructured() ) )
                {
                    line(ae->d_loc).ldc_i4(0);
                    line(ae->d_loc).ldelem_("char");
                }
            }
            break;
        case BuiltIn::CHR:
            Q_ASSERT( ae->d_args.size() == 1 );
            ae->d_args.first()->accept(this);
            break;
        case BuiltIn::FLT:
            {
                Q_ASSERT( ae->d_args.size() == 1 );
                ae->d_args.first()->accept(this);
                const int bt = ae->d_args.first()->d_type.isNull() ? 0 : ae->d_args.first()->d_type->getBaseType();
                if( bt != Type::REAL && bt != Type::LONGREAL )
                {
                    if( bt == Type::LONGINT )
                        line(ae->d_loc).conv_(IlEmitter::ToR8);
                    else
                        line(ae->d_loc).conv_(IlEmitter::ToR4);
                }
            }
            break;
        case BuiltIn::ODD:
            Q_ASSERT( ae->d_args.size() == 1 );
            ae->d_args.first()->accept(this);
            line(ae->d_loc).call_("bool [OBX.Runtime]OBX.Runtime::ODD(int32)", 1, true );
            break;
        case BuiltIn::ABS:
            {
                Q_ASSERT( ae->d_args.size() == 1 );
                ae->d_args.first()->accept(this);
                Type* t = derefed(ae->d_args.first()->d_type.data());
                Q_ASSERT( t );
                switch(t->getBaseType())
                {
                case Type::LONGREAL:
                    line(ae->d_loc).call_("float64 [mscorlib]System.Math::Abs(float64)", 1, true );
                    break;
                case Type::REAL:
                    line(ae->d_loc).call_("float32 [mscorlib]System.Math::Abs(float32)", 1, true );
                    break;
                case Type::LONGINT:
                    line(ae->d_loc).call_("int64 [mscorlib]System.Math::Abs(int64)", 1, true );
                    break;
                case Type::INTEGER:
                    line(ae->d_loc).call_("int32 [mscorlib]System.Math::Abs(int32)", 1, true );
                    break;
                case Type::SHORTINT:
                case Type::BYTE:
                    line(ae->d_loc).call_("int16 [mscorlib]System.Math::Abs(int16)", 1, true );
                    break;
                default:
                    Q_ASSERT(false);
                }
            }
            break;
        case BuiltIn::FLOOR:
            Q_ASSERT( ae->d_args.size() == 1 );
            ae->d_args.first()->accept(this);
            line(ae->d_loc).call_("float64 [mscorlib]System.Math::Floor(float64)", 1, true );
            line(ae->d_loc).conv_(IlEmitter::ToI4);
            break;
        case BuiltIn::LSL:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.first()->accept(this);
            ae->d_args.last()->accept(this);
            line(ae->d_loc).shl_();
            break;
        case BuiltIn::ASR:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.first()->accept(this);
            ae->d_args.last()->accept(this);
            line(ae->d_loc).shr_();
            break;
        case BuiltIn::ROR:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.first()->accept(this);
            ae->d_args.last()->accept(this);
            line(ae->d_loc).shr_(true);
            break;
        case BuiltIn::BITAND:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.first()->accept(this);
            ae->d_args.last()->accept(this);
            line(ae->d_loc).and_();
            break;
        case BuiltIn::BITOR:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.first()->accept(this);
            ae->d_args.last()->accept(this);
            line(ae->d_loc).or_();
            break;
        case BuiltIn::BITXOR:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.first()->accept(this);
            ae->d_args.last()->accept(this);
            line(ae->d_loc).xor_();
            break;
        case BuiltIn::BITNOT:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.first()->accept(this);
            ae->d_args.last()->accept(this);
            line(ae->d_loc).not_();
            break;
        case BuiltIn::SHORT:
            Q_ASSERT( ae->d_args.size() == 1 );
            ae->d_args.first()->accept(this);
            switch(derefed(ae->d_args.first()->d_type.data())->getBaseType())
            {
            case Type::LONGINT:
                line(ae->d_loc).conv_(IlEmitter::ToI4);
                break;
            case Type::INTEGER:
                line(ae->d_loc).conv_(IlEmitter::ToI2);
                break;
            case Type::SHORTINT:
                line(ae->d_loc).conv_(IlEmitter::ToU1);
                break;
            case Type::LONGREAL:
                line(ae->d_loc).conv_(IlEmitter::ToR4);
                break;
            default:
                Q_ASSERT(false);
            }
            break;
        case BuiltIn::LONG:
            Q_ASSERT( ae->d_args.size() == 1 );
            ae->d_args.first()->accept(this);
            switch(derefed(ae->d_args.first()->d_type.data())->getBaseType())
            {
            case Type::INTEGER:
                line(ae->d_loc).conv_(IlEmitter::ToI8);
                break;
            case Type::SHORTINT:
                line(ae->d_loc).conv_(IlEmitter::ToI4);
                break;
            case Type::BYTE:
                line(ae->d_loc).conv_(IlEmitter::ToI2);
                break;
            case Type::REAL:
                line(ae->d_loc).conv_(IlEmitter::ToR8);
                break;
            default:
                Q_ASSERT(false);
            }
            break;
        case BuiltIn::ADR:
            Q_ASSERT( ae->d_args.size() == 1 );
            ae->d_args.first()->accept(this);
            break;
        case BuiltIn::VAL:
            Q_ASSERT( ae->d_args.size() == 2 );
            ae->d_args.last()->accept(this);
            break;
        case BuiltIn::ASSERT:
            {
                Q_ASSERT( !ae->d_args.isEmpty() ); // TODO: by now second optional arg ignored!
                ae->d_args.first()->accept(this);

                const int after = emitter->newLabel();
                line(ae->d_loc).brtrue_(after);
                line(ae->d_loc).ldstr_("\"assertion failed at line "+QByteArray::number(ae->d_loc.d_row)+"\"");
                line(ae->d_loc).newobj_("void [mscorlib]System.Exception::.ctor(string)",1);
                line(ae->d_loc).throw_();

                line(ae->d_loc).label_(after);
            }
            break;
        case BuiltIn::BYTESIZE:
            {
                Q_ASSERT( !ae->d_args.isEmpty() && !ae->d_args.first()->d_type.isNull() );
                Expression* e = ae->d_args.first().data();
                Type* t = derefed(e->d_type.data());
                switch( t->getBaseType() )
                {
                case Type::BOOLEAN:
                case Type::CHAR:
                case Type::BYTE:
                    line(ae->d_loc).ldc_i4(1);
                    break;
                case Type::WCHAR:
                case Type::SHORTINT:
                    line(ae->d_loc).ldc_i4(2);
                    break;
                case Type::INTEGER:
                case Type::REAL:
                case Type::SET:
                    line(ae->d_loc).ldc_i4(4);
                    break;
                case Type::LONGINT:
                case Type::LONGREAL:
                    line(ae->d_loc).ldc_i4(8);
                    break;
                default:
                    switch( t->getTag() )
                    {
                    case Thing::T_Pointer:
                        line(ae->d_loc).ldc_i4(4);// TODO
                        break;
                    case Thing::T_Record:
                    case Thing::T_Array:
                        line(ae->d_loc).ldc_i4(1);// TODO
                        break;
                    default:
                        Q_ASSERT( false ); // TODO
                        break;
                    }
                    break;
                }
            }
            break;
        default:
             qWarning() << "missing generator implementation of" << BuiltIn::s_typeName[bi->d_func];
             break;
        }
    }

    static inline bool passByRef( Parameter* p )
    {
        if( !p->d_var || p->d_const )
            return false;
        Type* td = derefed(p->d_type.data());
        if( td && !td->isStructured() )
            return true; // we only need to pass simple types including pointers and proc refs by &
        else
            return false; // all our structured values are already on the heap, the value is actually a object reference
    }

    void emitCall( ArgExpr* me )
    {
        Q_ASSERT( me->d_sub );
        me->d_sub->accept(this);

        Named* func = 0;
        bool superCall = false;
        if( me->d_sub->getUnOp() == UnExpr::DEREF )
        {
            // call to superclass method
            UnExpr* ue = cast<UnExpr*>(me->d_sub.data());
            func = ue->d_sub->getIdent();
            Q_ASSERT( func && func->getTag() == Thing::T_Procedure );
            Procedure* p = cast<Procedure*>(func);
            Q_ASSERT( p->d_super );
            func = p->d_super;
            superCall = true;
        }else
            func = me->d_sub->getIdent();

        const int funcTag = func ? func->getTag() : 0;
        if( func && funcTag == Thing::T_BuiltIn )
        {
            emitBuiltIn( cast<BuiltIn*>(func), me );
            return;
        }else if( funcTag != Thing::T_Procedure )
            func = 0; // apparently a function pointer or delegate

        Type* subT = derefed( me->d_sub->d_type.data() );
        Q_ASSERT( subT && subT->getTag() == Thing::T_ProcType );
        ProcType* pt = cast<ProcType*>( subT );
        Q_ASSERT( pt->d_formals.size() <= me->d_args.size() );

        if( func == 0 || pt->d_typeBound )
            ; // Q_ASSERT( stackDepth == before + 1 ); // self or delegate instance expected

        for( int i = 0; i < pt->d_formals.size(); i++ )
        {
            Parameter* p = pt->d_formals[i].data();
            Type* tf = derefed(p->d_type.data());
            Q_ASSERT( tf != 0 );

            if( passByRef(p) )
            {
                if( tf->getTag() == Thing::T_Array )
                {
                    Array* la = cast<Array*>(tf);
                    Type* ta = derefed(me->d_args[i]->d_type.data());
                    Q_ASSERT( ta != 0 );
                    Type* rat = ta->getTag() == Thing::T_Array ? derefed(cast<Array*>(ta)->d_type.data()) : 0;
                    if( derefed(la->d_type.data())->getBaseType() == Type::BYTE &&
                            ( rat == 0 || rat->getBaseType() != Type::BYTE ) )
                    {
                        err->error( Errors::Generator, Loc(me->d_args[i]->d_loc, thisMod->d_file),
                                      "cannot generate code for Oberon VAR ARRAY OF BYTE trick");
                        continue;
                    }
                }
                emitFetchDesigAddr(me->d_args[i].data());
            }else
            {
                // 1) a structured arg (record, array) passed by val
                // 2) or a structured arg passed to IN, i.e. just pass the reference
                // 3) or a non-structured arg passed by IN or by val, just pass the value in both cases
                // NOTE that in case of 1) the copy is done in the body of the called procedure
                me->d_args[i]->accept(this);
                prepareRhs( tf, me->d_args[i].data(), me->d_args[i]->d_loc );
            }
        }

        // TODO varargs
        if( func )
        {
            if( pt->d_typeBound && !superCall )
                line(me->d_loc).callvirt_(memberRef(func),pt->d_formals.size(),!pt->d_return.isNull());
            else
                line(me->d_loc).call_(memberRef(func),pt->d_formals.size(),!pt->d_return.isNull());
        }else
        {
            const QByteArray what = formatType(pt->d_return.data()) + " class " + delegateRef(pt) + "::Invoke"
                + formatFormals(pt->d_formals,false);
            line(me->d_loc).callvirt_(what, pt->d_formals.size(),!pt->d_return.isNull());
        }
    }

    void inline prepareRhs(Type* tf, Expression* ea, const RowCol& loc)
    {
        Q_ASSERT(ea);
        tf = derefed(tf);
        Q_ASSERT( tf != 0 );
        Type* ta = derefed(ea->d_type.data());
        if( ta == 0 )
            return; // error already reported

        if( tf->isChar() && !ta->isChar() )
        {
            // convert len-1-string to char
            Q_ASSERT( ta->isString() || ta->isStructured() );
            line(loc).ldc_i4(0);
            line(loc).ldelem_("char");
        }else if( tf->isText() && !tf->isChar() && ta->isChar() )
        {
            line(loc).call_("char[] [OBX.Runtime]OBX.Runtime::toString(char)", 1, true );
        }else if( tf->getTag() == Thing::T_ProcType )
        {
            Named* n = ea->getIdent();
            if( n && n->getTag() == Thing::T_Procedure )
            {
                ProcType* pt = cast<ProcType*>(tf);

                if( ta->d_typeBound )
                {
                    // we assign a type bound procedure to a type-bound proc type variable
                    // for this purpose we create a delegate instance on the stack
                    line(loc).dup_(); // stack: this, this
                    line(loc).ldvirtftn_(memberRef(n)); // stack: this, fn
                    line(loc).newobj_("void class " + delegateRef(pt) + "::.ctor(object, native unsigned int)", 2 );
                }else
                {
                    // assign a normal procedure to a normal proc type variable
                    line(loc).ldnull_();
                    line(loc).ldftn_(memberRef(n));
                    line(loc).newobj_("void class " + delegateRef(pt) + "::.ctor(object, native unsigned int)",2);
                }
            }//else: we copy a proc type variable, i.e. delegate already exists
        }
    }

    void visit( ArgExpr* me )
    {
        switch( me->d_op )
        {
        case ArgExpr::IDX:
            emitIndex(me);
            break;
        case ArgExpr::CALL:
            emitCall(me);
            break;
        case ArgExpr::CAST:
            me->d_sub->accept(this);
            break;
        }
    }

    void stringOp( bool lhsChar, bool rhsChar, int op, const RowCol& loc )
    {
        line(loc).ldc_i4(op);
        if( lhsChar && rhsChar )
            line(loc).call_("bool [OBX.Runtime]OBX.Runtime::relOp(char,char,int32)",3,true);
        else if( lhsChar && !rhsChar )
            line(loc).call_("bool [OBX.Runtime]OBX.Runtime::relOp(char,char[],int32)",3,true);
        else if( !lhsChar && rhsChar )
            line(loc).call_("bool [OBX.Runtime]OBX.Runtime::relOp(char[],char,int32)",3,true);
        else
            line(loc).call_("bool [OBX.Runtime]OBX.Runtime::relOp(char[],char[],int32)",3,true);
    }

    void convertTo( quint8 toBaseType, Type* from, const RowCol& loc )
    {
        from = derefed(from);
        if( from == 0 )
            return;
        if( toBaseType == from->getBaseType() )
            return;
        switch( toBaseType )
        {
        case Type::LONGREAL:
            line(loc).conv_(IlEmitter::ToR8);
            break;
        case Type::REAL:
            line(loc).conv_(IlEmitter::ToR4);
            break;
        case Type::LONGINT:
            line(loc).conv_(IlEmitter::ToI8);
            break;
        case Type::INTEGER:
        case Type::SET:
            line(loc).conv_(IlEmitter::ToI4);
            break;
        case Type::SHORTINT:
        case Type::CHAR:
        case Type::WCHAR:
            line(loc).conv_(IlEmitter::ToI2);
            break;
        case Type::BYTE:
        case Type::BOOLEAN:
            line(loc).conv_(IlEmitter::ToU1);
            break;
        }
    }

    void visit( BinExpr* me)
    {
        Q_ASSERT( !me->d_lhs.isNull() && !me->d_rhs.isNull() &&
                  !me->d_lhs->d_type.isNull() && !me->d_rhs->d_type.isNull() );

        me->d_lhs->accept(this);
        if( me->isArithRelation() )
            convertTo(me->d_baseType, me->d_lhs->d_type.data(), me->d_lhs->d_loc);

        if( me->d_op != BinExpr::AND && me->d_op != BinExpr::OR )
        {
            // AND and OR are special in that rhs might not be executed
            me->d_rhs->accept(this);
            if( me->isArithRelation() )
                convertTo(me->d_baseType, me->d_rhs->d_type.data(), me->d_rhs->d_loc );
        }

        Type* lhsT = derefed(me->d_lhs->d_type.data());
        Type* rhsT = derefed(me->d_rhs->d_type.data());
        Q_ASSERT( lhsT && rhsT );
        const int ltag = lhsT->getTag();
        const int rtag = rhsT->getTag();
        bool lwide, rwide;

        switch( me->d_op )
        {
        case BinExpr::IN:
            if( lhsT->isInteger() && rhsT->getBaseType() == Type::SET )
            {
                line(me->d_loc).call_("bool [OBX.Runtime]OBX.Runtime::IN(int32, int32)", 2, true );
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::IS:
            line(me->d_loc).isinst_(formatType(rhsT));// returns object or null
            line(me->d_loc).ldnull_();
            line(me->d_loc).ceq_(); // true if null
            line(me->d_loc).ldc_i4(0);
            line(me->d_loc).ceq_(); // not
            break;
        case BinExpr::ADD:
            if( ( lhsT->isNumeric() && rhsT->isNumeric() ) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) )
                line(me->d_loc).add_();
            else if( lhsT->isSet() && rhsT->isSet() )
                line(me->d_loc).or_();
            else if( lhsT->isText(&lwide) && rhsT->isText(&rwide) )
            {
                if( lhsT->isChar() && rhsT->isChar() )
                    line(me->d_loc).call_("char[] [OBX.Runtime]OBX.Runtime::join(char,char)", 2, true );
                else if( lhsT->isChar() && !rhsT->isChar() )
                    line(me->d_loc).call_("char[] [OBX.Runtime]OBX.Runtime::join(char,char[])",2,true);
                else if( !lhsT->isChar() && rhsT->isChar() )
                    line(me->d_loc).call_("char[] [OBX.Runtime]OBX.Runtime::join(char[],char)",2,true);
                else
                    line(me->d_loc).call_("char[] [OBX.Runtime]OBX.Runtime::join(char[],char[])",2,true);
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::SUB:
            if( (lhsT->isNumeric() && rhsT->isNumeric()) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) )
                line(me->d_loc).sub_();
            else if( lhsT->isSet() && rhsT->isSet() )
            {
                line(me->d_loc).not_();
                line(me->d_loc).and_();
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::FDIV:
            if( lhsT->isNumeric() && rhsT->isNumeric() )
                line(me->d_loc).div_();
            else if( lhsT->isSet() && rhsT->isSet() )
            {
                const int rhs = temps.buy();
                line(me->d_loc).stloc_(rhs);
                const int lhs = temps.buy();
                line(me->d_loc).stloc_(lhs);
                line(me->d_loc).ldloc_(lhs);
                line(me->d_loc).ldloc_(rhs);
                line(me->d_loc).and_();
                line(me->d_loc).not_();
                line(me->d_loc).ldloc_(lhs);
                line(me->d_loc).ldloc_(rhs);
                line(me->d_loc).or_();
                line(me->d_loc).and_();
                temps.sell(rhs);
                temps.sell(lhs);
            }
            break;
        case BinExpr::MUL:
            if( lhsT->isNumeric() && rhsT->isNumeric() )
                line(me->d_loc).mul_();
            else if( lhsT->isSet() && rhsT->isSet() )
                line(me->d_loc).and_();
            else
                Q_ASSERT(false);
            break;
        case BinExpr::DIV:
            if( lhsT->isInteger() && rhsT->isInteger() )
            {
                if( lhsT->getBaseType() <= Type::INTEGER && rhsT->getBaseType() <= Type::INTEGER )
                    line(me->d_loc).call_("int32 [OBX.Runtime]OBX.Runtime::DIV(int32,int32)",2,true );
                else
                    line(me->d_loc).call_("int64 [OBX.Runtime]OBX.Runtime::DIV(int64,int64)",2,true );
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::MOD:
            if( lhsT->isInteger() && rhsT->isInteger() )
            {
                if( lhsT->getBaseType() <= Type::INTEGER && rhsT->getBaseType() <= Type::INTEGER )
                    line(me->d_loc).call_("int32 [OBX.Runtime]OBX.Runtime::MOD(int32,int32)",2,true);
                else
                    line(me->d_loc).call_("int64 [OBX.Runtime]OBX.Runtime::MOD(int64,int64)",2,true);
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::AND:
            if( lhsT->getBaseType() == Type::BOOLEAN && rhsT->getBaseType() == Type::BOOLEAN )
            {
                // lhs was run and stack has a bool result
                const int afterEnd = emitter->newLabel();
                const int setFalse = emitter->newLabel();
                line(me->d_loc).brfalse_(setFalse);
                me->d_rhs->accept(this);
                line(me->d_loc).br_(afterEnd);
                line(me->d_loc).label_(setFalse);
                line(me->d_loc).ldc_i4(0);
                line(me->d_loc).label_(afterEnd);
                // TODO stackDepth--; // correct for alternative rhs or ldc
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::OR:
            if( lhsT->getBaseType() == Type::BOOLEAN && rhsT->getBaseType() == Type::BOOLEAN )
            {
                // lhs was run and stack has a bool result
                const int afterEnd = emitter->newLabel();
                const int setTrue = emitter->newLabel();
                line(me->d_loc).brtrue_(setTrue);
                me->d_rhs->accept(this);
                line(me->d_loc).br_(afterEnd);
                line(me->d_loc).label_(setTrue);
                line(me->d_loc).ldc_i4(1);
                line(me->d_loc).label_(afterEnd);
                // TODO stackDepth--; // correct for alternative rhs or ldc
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::EQ:
            if( ( lhsT->isNumeric() && rhsT->isNumeric() ) ||
                    ( lhsT->getBaseType() == Type::BOOLEAN && rhsT->getBaseType() == Type::BOOLEAN ) ||
                    ( lhsT->getBaseType() == Type::SET && rhsT->getBaseType() == Type::SET) ||
                    ( lhsT->isChar() && rhsT->isChar() ) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) ||
                    ( ( lhsT->getBaseType() == Type::NIL || ltag == Thing::T_Pointer || ltag == Thing::T_ProcType ) &&
                    ( rhsT->getBaseType() == Type::NIL || rtag == Thing::T_Pointer || rtag == Thing::T_ProcType ) ) )
            {
                line(me->d_loc).ceq_();
            }else if( lhsT->isText(&lwide) && rhsT->isText(&rwide) )
            {
                stringOp(lhsT->isChar(), rhsT->isChar(), 1, me->d_loc );
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::NEQ:
            if( ( lhsT->isNumeric() && rhsT->isNumeric() ) ||
                    ( lhsT->getBaseType() == Type::BOOLEAN && rhsT->getBaseType() == Type::BOOLEAN ) ||
                    ( lhsT->getBaseType() == Type::SET && rhsT->getBaseType() == Type::SET) ||
                    ( lhsT->isChar() && rhsT->isChar() ) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) ||
                    ( ( lhsT->getBaseType() == Type::NIL || ltag == Thing::T_Pointer || ltag == Thing::T_ProcType ) &&
                    ( rhsT->getBaseType() == Type::NIL || rtag == Thing::T_Pointer || rtag == Thing::T_ProcType ) ) )
            {
                line(me->d_loc).ceq_();
                line(me->d_loc).ldc_i4(0);
                line(me->d_loc).ceq_();
            }else if( lhsT->isText(&lwide) && rhsT->isText(&rwide) )
            {
                stringOp(lhsT->isChar(), rhsT->isChar(), 2, me->d_loc );
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::LT:
            if( ( lhsT->isNumeric() && rhsT->isNumeric() ) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) ||
                    ( lhsT->isChar() && rhsT->isChar() ) )
            {
                line(me->d_loc).clt_();
            }else if( lhsT->isText(&lwide) && rhsT->isText(&rwide) )
            {
                stringOp(lhsT->isChar(), rhsT->isChar(), 3, me->d_loc );
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::LEQ:
            if( ( lhsT->isNumeric() && rhsT->isNumeric() ) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) ||
                    ( lhsT->isChar() && rhsT->isChar() ) )
            {
                line(me->d_loc).cgt_();
                line(me->d_loc).ldc_i4(0);
                line(me->d_loc).ceq_();
            }else if( lhsT->isText(&lwide) && rhsT->isText(&rwide) )
            {
                stringOp(lhsT->isChar(), rhsT->isChar(), 4, me->d_loc );
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::GT:
            if( ( lhsT->isNumeric() && rhsT->isNumeric() ) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) ||
                    ( lhsT->isChar() && rhsT->isChar() ) )
            {
                line(me->d_loc).cgt_();
            }else if( lhsT->isText(&lwide) && rhsT->isText(&rwide) )
            {
                stringOp(lhsT->isChar(), rhsT->isChar(), 5, me->d_loc );
            }else
                Q_ASSERT(false);
            break;
        case BinExpr::GEQ:
            if( ( lhsT->isNumeric() && rhsT->isNumeric() ) ||
                    ( ltag == Thing::T_Enumeration && rtag == Thing::T_Enumeration ) ||
                    ( lhsT->isChar() && rhsT->isChar() ) )
            {
                line(me->d_loc).clt_();
                line(me->d_loc).ldc_i4(0);
                line(me->d_loc).ceq_();
            }else if( lhsT->isText(&lwide) && rhsT->isText(&rwide) )
            {
                stringOp(lhsT->isChar(), rhsT->isChar(), 6, me->d_loc );
            }else
                Q_ASSERT(false);
            break;
        default:
            Q_ASSERT(false);
        }
    }

    void visit( SetExpr* me)
    {
        line(me->d_loc).ldc_i4(0);
        for( int i = 0; i < me->d_parts.size(); i++ )
        {
            BinExpr* bi = me->d_parts[i]->getTag() == Thing::T_BinExpr ? cast<BinExpr*>( me->d_parts[i].data() ) : 0;
            if( bi && bi->d_op == BinExpr::Range )
            {
                // set or 0 already on stack
                if( bi->d_lhs )
                    bi->d_lhs->accept(this);
                if( bi->d_rhs )
                    bi->d_rhs->accept(this);
                // set, from and to index on stack
                line(me->d_loc).call_("int32 [OBX.Runtime]OBX.Runtime::addRangeToSet(int32, int32, int32)",3,true);
                // new set on stack
            }else
            {
                // set or 0 already on stack
                me->d_parts[i]->accept(this);
                // element index on stack
                line(me->d_loc).call_("int32 [OBX.Runtime]OBX.Runtime::addElemToSet(int32, int32)",2,true);
                // new set on stack
            }
        }
    }

    void visit( Call* me)
    {
        Q_ASSERT( me->d_what );
        me->d_what->accept(this);
        if( !me->d_what->d_type.isNull() )
        {
            Type* td = derefed(me->d_what->d_type.data());
            if(td && td->getTag() == Thing::T_BaseType && td->getBaseType() != Type::NONE )
                line(me->d_loc).pop_();
        }
    }

    void visit( ForLoop* me)
    {
        //const int before = stackDepth;
        // i := from;
        // WHILE i <= to DO statements; i := i + by END
        // WHILE i >= to DO statements; i := i + by END

        Ref<Assign> a = new Assign();
        a->d_loc = me->d_loc;
        a->d_lhs = me->d_id;
        a->d_rhs = me->d_from;

        Ref<IfLoop> loop = new IfLoop();
        loop->d_loc = me->d_loc;
        loop->d_op = IfLoop::WHILE;

        Ref<BinExpr> cond = new BinExpr();
        cond->d_loc = me->d_loc;
        if( me->d_byVal.toInt() > 0 )
            cond->d_op = BinExpr::LEQ;
        else
            cond->d_op = BinExpr::GEQ;
        cond->d_lhs = me->d_id;
        cond->d_rhs = me->d_to;
        cond->d_type = me->d_id->d_type.data();
        loop->d_if.append( cond.data() );

        loop->d_then.append( me->d_do );

        Ref<BinExpr> add = new BinExpr();
        add->d_loc = me->d_loc;
        add->d_op = BinExpr::ADD;
        add->d_lhs = me->d_id;
        add->d_rhs = me->d_by;
        add->d_type = me->d_by->d_type;

        Ref<Assign> a2 = new Assign();
        a2->d_loc = me->d_loc;
        a2->d_lhs = me->d_id;
        a2->d_rhs = add.data();

        loop->d_then.back().append( a2.data() );

        a->accept(this);
        loop->accept(this);
        // TODO Q_ASSERT( before == stackDepth );
    }

    void emitIf( IfLoop* me)
    {
        me->d_if[0]->accept(this); // IF
        const int afterFirst = emitter->newLabel();
        line(me->d_loc).brfalse_(afterFirst);

        for( int i = 0; i < me->d_then[0].size(); i++ )
            me->d_then[0][i]->accept(this);

        const int afterEnd = emitter->newLabel();
        line(me->d_loc).br_(afterEnd);

        line(me->d_loc).label_(afterFirst);
        for( int i = 1; i < me->d_if.size(); i++ ) // ELSIF
        {
            me->d_if[i]->accept(this);
            const int afterNext = emitter->newLabel();
            line(me->d_loc).brfalse_(afterNext);

            for( int j = 0; j < me->d_then[i].size(); j++ )
                me->d_then[i][j]->accept(this);

            line(me->d_loc).br_(afterEnd);

            line(me->d_loc).label_(afterNext);
        }

        if( !me->d_else.isEmpty() ) // ELSE
        {
            for( int j = 0; j < me->d_else.size(); j++ )
                me->d_else[j]->accept(this);
        }

        line(me->d_loc).label_(afterEnd);
    }

    void visit( IfLoop* me)
    {
        //const int before = stackDepth;
        switch( me->d_op )
        {
        case IfLoop::IF:
            emitIf(me);
            break;
        case IfLoop::WHILE:
            {
                // substitute by primitive statements
                Ref<IfLoop> loop = new IfLoop();
                loop->d_op = IfLoop::LOOP;
                loop->d_loc = me->d_loc;

                Ref<IfLoop> conds = new IfLoop();
                conds->d_op = IfLoop::IF;
                conds->d_loc = me->d_loc;

                conds->d_if = me->d_if;
                conds->d_then = me->d_then;

                Q_ASSERT( me->d_else.isEmpty() );
                Ref<Exit> ex = new Exit();
                ex->d_loc = me->d_loc;
                conds->d_else << ex.data();

                loop->d_then << ( StatSeq() << conds.data() );

                loop->accept(this); // now render
            }
            break;
        case IfLoop::REPEAT:
            {
                const int loopStart = emitter->newLabel();
                line(me->d_loc).label_(loopStart);

                for( int i = 0; i < me->d_then.first().size(); i++ )
                    me->d_then.first()[i]->accept(this);

                me->d_if[0]->accept(this); // until condition
                const int afterEnd = emitter->newLabel();
                line(me->d_loc).brtrue_(afterEnd);

                line(me->d_loc).br_(loopStart);

                line(me->d_loc).label_(afterEnd);
            }
            break;
        case IfLoop::WITH:
            {
                // if guard then statseq elsif guard then statseq else statseq end
                // guard ::= lhs IS rhs
                emitIf(me);
            }
            break;
        case IfLoop::LOOP:
            {
                Q_ASSERT( exitJump == -1 );
                const int loopStart = emitter->newLabel();
                line(me->d_loc).label_(loopStart);

                for( int i = 0; i < me->d_then.first().size(); i++ )
                    me->d_then.first()[i]->accept(this);

                line(me->d_loc).br_(loopStart);

                if( exitJump != -1 )
                    line(me->d_loc).label_(exitJump);
                exitJump = -1;
            }
            break;
        }
        // no, it is legal and not known here how many values are pushed in the body: Q_ASSERT( before == stackDepth );
    }

    void visit( Assign* me )
    {
        Q_ASSERT( me->d_rhs );

        Q_ASSERT( me->d_lhs );

        Q_ASSERT( !me->d_lhs->d_type.isNull() );
        Q_ASSERT( !me->d_rhs->d_type.isNull() );

        Type* lhsT = derefed(me->d_lhs->d_type.data());
        Q_ASSERT( lhsT != 0 );

        if( lhsT->isStructured() )
        {
            me->d_lhs->accept(this);
            me->d_rhs->accept(this);
            prepareRhs(lhsT, me->d_rhs.data(), me->d_loc );
            switch(lhsT->getTag())
            {
            case Thing::T_Record:
                {
                    // stack: lhs record, rhs record
                    Record* r = cast<Record*>(lhsT);
                    QByteArray what = "void class " + classRef(r) + formatMetaActuals(r) + "::'#copy'(";
                    what += formatType(r);
                    if( r->d_byValue )
                        what += "&";
                    what += ")";
                    line(me->d_loc).callvirt_(what,1);
                }
                break;
            case Thing::T_Array:
                {
                    // stack: lhs array, lhs array, rhs array
                    line(me->d_loc).call_(formatArrayCopierRef(cast<Array*>(lhsT)),2);
                }
                break;
            default:
                Q_ASSERT(false);
            }
        }else
        {
            emitFetchDesigAddr(me->d_lhs.data());
            me->d_rhs->accept(this);
            prepareRhs(lhsT, me->d_rhs.data(), me->d_loc );
#if _USE_LDSTOBJ
            convertTo(lhsT->getBaseType(),me->d_rhs->d_type.data(), me->d_loc); // required, otherwise crash when LONGREAL
            line(me->d_loc).stobj_(formatType(me->d_lhs->d_type.data()));
#else
            switch( lhsT->getTag() )
            {
            case Thing::T_Pointer:
            case Thing::T_ProcType:
                emitOpcode("stind.ref",-2, me->d_loc);
                break;
            case Thing::T_Enumeration:
                emitOpcode("stind.i4",-2, me->d_loc);
                break;
            case Thing::T_BaseType:
                switch( lhsT->getBaseType() )
                {
                case Type::LONGREAL:
                    emitOpcode("conv.r8",0,me->d_loc);
                    emitOpcode("stind.r8",-2, me->d_loc);
                    break;
                case Type::REAL:
                    emitOpcode("conv.r4",0,me->d_loc);
                    emitOpcode("stind.r4",-2, me->d_loc);
                    break;
                case Type::LONGINT:
                    emitOpcode("conv.i8",0,me->d_loc);
                    emitOpcode("stind.i8",-2, me->d_loc);
                    break;
                case Type::INTEGER:
                case Type::SET:
                    emitOpcode("conv.i4",0,me->d_loc);
                    emitOpcode("stind.i4",-2, me->d_loc);
                    break;
                case Type::SHORTINT:
                case Type::CHAR:
                case Type::WCHAR:
                    emitOpcode("conv.i2",0,me->d_loc);
                    emitOpcode("stind.i2",-2, me->d_loc);
                    break;
                case Type::BYTE:
                case Type::BOOLEAN:
                    emitOpcode("conv.u1",0,me->d_loc);
                    emitOpcode("stind.i1",-2, me->d_loc);
                    break;
                default:
                    Q_ASSERT(false);
                }
                break;
            default:
                Q_ASSERT(false);
            }
#endif
        }
    }

    void visit( CaseStmt* me)
    {
        if( me->d_typeCase )
        {
            // first rewrite the AST with 'if' instead of complex 'case'

            if( me->d_cases.isEmpty() )
                return;

            Ref<IfLoop> ifl = new IfLoop();
            ifl->d_op = IfLoop::IF;
            ifl->d_loc = me->d_loc;

            for( int i = 0; i < me->d_cases.size(); i++ )
            {
                const CaseStmt::Case& c = me->d_cases[i];

                Q_ASSERT( c.d_labels.size() == 1 );

                Ref<BinExpr> eq = new BinExpr();
                eq->d_op = BinExpr::IS;
                eq->d_lhs = me->d_exp;
                eq->d_rhs = c.d_labels.first();
                eq->d_loc = me->d_exp->d_loc;
                eq->d_type = new BaseType(Type::BOOLEAN);

                ifl->d_if.append(eq.data());
                ifl->d_then.append( c.d_block );
            }

            // and now generate code for the if
            ifl->accept(this);
        }
        else
        {
            // first rewrite the AST with 'if' instead of complex 'case'

            Ref<IfLoop> ifl = new IfLoop();
            ifl->d_op = IfLoop::IF;
            ifl->d_loc = me->d_loc;

            Ref<BaseType> boolean = new BaseType(Type::BOOLEAN);

            for( int i = 0; i < me->d_cases.size(); i++ )
            {
                const CaseStmt::Case& c = me->d_cases[i];

                QList< Ref<Expression> > ors;
                for( int j = 0; j < c.d_labels.size(); j++ )
                {
                    Expression* l = c.d_labels[j].data();
                    // TODO: avoid redundant evaluations using temp vars
                    bool done = false;
                    if( l->getTag() == Thing::T_BinExpr )
                    {
                        BinExpr* bi = cast<BinExpr*>( l );
                        if( bi->d_op == BinExpr::Range )
                        {
                            Ref<BinExpr> _and = new BinExpr();
                            _and->d_op = BinExpr::AND;
                            _and->d_loc = l->d_loc;
                            _and->d_type = boolean.data();

                            Ref<BinExpr> lhs = new BinExpr();
                            lhs->d_op = BinExpr::GEQ;
                            lhs->d_lhs = me->d_exp;
                            lhs->d_rhs = bi->d_lhs;
                            lhs->d_loc = l->d_loc;
                            lhs->d_type = boolean.data();

                            Ref<BinExpr> rhs = new BinExpr();
                            rhs->d_op = BinExpr::LEQ;
                            rhs->d_lhs = me->d_exp;
                            rhs->d_rhs = bi->d_rhs;
                            rhs->d_loc = l->d_loc;
                            rhs->d_type = boolean.data();

                            _and->d_lhs = lhs.data();
                            _and->d_rhs = rhs.data();

                            ors << _and.data();
                            done = true;
                        }
                    }
                    if( !done )
                    {
                        Ref<BinExpr> eq = new BinExpr();
                        eq->d_op = BinExpr::EQ;
                        eq->d_lhs = me->d_exp;
                        eq->d_rhs = l;
                        eq->d_loc = l->d_loc;
                        eq->d_type = boolean.data();

                        ors << eq.data();
                    }
                }
                Q_ASSERT( !ors.isEmpty() );
                if( ors.size() == 1 )
                    ifl->d_if.append( ors.first() );
                else
                {
                    Q_ASSERT( ors.size() > 1 );
                    Ref<BinExpr> bi = new BinExpr();
                    bi->d_op = BinExpr::OR;
                    bi->d_lhs = ors[0];
                    bi->d_rhs = ors[1];
                    bi->d_loc = ors[1]->d_loc;
                    bi->d_type = boolean.data();
                    for( int i = 2; i < ors.size(); i++ )
                    {
                        Ref<BinExpr> tmp = new BinExpr();
                        tmp->d_op = BinExpr::OR;
                        tmp->d_lhs = bi.data();
                        tmp->d_type = boolean.data();
                        bi = tmp;
                        bi->d_rhs = ors[i];
                        bi->d_loc = ors[i]->d_loc;
                    }
                    ifl->d_if.append( bi.data() );
                }

                ifl->d_then.append( c.d_block );
            }

            // and now generate code for the if
            ifl->accept(this);
        }
    }

    void visit( Exit* me)
    {
        if( exitJump < 0 )
            exitJump = emitter->newLabel();
        else
            Q_ASSERT( false );
        line(me->d_loc).br_(exitJump);
    }

    void emitReturn(ProcType* pt, Expression* what, const RowCol& loc)
    {
        Q_ASSERT( pt );

        if( what )
        {
            Type* lt = pt->d_return.data();
            Type* ltd = derefed(lt);
            if( ltd && ltd->isStructured() )
            {
                emitInitializer(lt,false,loc); // create new record or array
                line(loc).dup_();
                what->accept(this);
                switch(ltd->getTag())
                {
                case Thing::T_Record:
                    {
                        // stack: new record, new record, rhs record
                        Record* r = cast<Record*>(ltd);
                        QByteArray what = "void class " + classRef(r) + formatMetaActuals(r) + "::'#copy'(";
                        what += formatType(r);
                        if( r->d_byValue )
                            what += "&";
                        what += ")";
                        line(loc).callvirt_(what,1);
                    }
                    break;
                case Thing::T_Array:
                    {
                        // stack: new array, new array, rhs array
                        line(loc).call_(formatArrayCopierRef(cast<Array*>(ltd)),2);
                    }
                    break;
                default:
                    Q_ASSERT(false);
                }
            }else
            {
                what->accept(this);
                prepareRhs( ltd, what, loc );
            }
            line(loc).ret_(true);
        }else if( !pt->d_return.isNull() )
        {
            // a function with no body; return default value
            if( !emitInitializer(pt->d_return.data(),false,loc) )
                line(loc).ldnull_(); // only happens for pointer and proctype
            line(loc).ret_(true);
        }else
        {
            line(loc).ret_();
        }
    }

    void visit( Return* me )
    {
        Q_ASSERT( scope != 0 );
        emitReturn( scope->getProcType(), me->d_what.data(), me->d_loc );
    }

    static inline Type* derefed( Type* t )
    {
        if( t )
            return t->derefed();
        else
            return 0;
    }

    IlEmitter& line(const RowCol& loc)
    {
        if( !(loc == last) )
        {
            emitter->line_(loc);
            last = loc;
        }
        return *emitter;
    }

    bool emitInitializer( Type* t, bool resolvePtr, const RowCol& loc, const QList<int>& lengths = QList<int>() )
    {
        // note that this proc is also called if t is a pointer

        // expects non-derefed t!
        Type* td = derefed(t);
        Q_ASSERT( td );

        if( resolvePtr && td->getTag() == Thing::T_Pointer )
            td = derefed(cast<Pointer*>(td)->d_to.data());

        switch( td->getTag() )
        {
        case Thing::T_BaseType:
            // at least the oberon system assumes initialized module variables
            switch( td->getBaseType() )
            {
            case Type::BOOLEAN:
            case Type::CHAR:
            case Type::WCHAR:
            case Type::BYTE:
            case Type::SHORTINT:
            case Type::INTEGER:
            case Type::SET:
                line(loc).ldc_i4(0);
                break;
            case Type::LONGINT:
                line(loc).ldc_i8(0);
                break;
            case Type::REAL:
                line(loc).ldc_r4(0.0);
                break;
            case Type::LONGREAL:
                line(loc).ldc_r8(0.0);
                break;
#ifndef _MY_GENERICS_
            case Type::ANY:
                {
                    QByteArray name = formatType(t);
                    emitOpcode2("ldsfld ",  1, loc );
                    out << name;
                    if( name.startsWith('\'') )
                        name = name.mid(1, name.size() - 2 );
                    name = name.mid(1); // get rid of !
                    out << " class " << moduleRef(thisMod) << formatMetaActuals(thisMod) << "::'##" << name << "'" << endl;
                }
                break;
#endif
            default:
                Q_ASSERT( false );
                break;
            }
            return true;
        case Thing::T_Enumeration:
            line(loc).ldc_i4(0);
            return true;
        case Thing::T_ProcType:
        case Thing::T_Pointer:
#if 0 // not needed with CLI
            emitOpcode("ldnull",1,loc);
            Q_ASSERT( stackDepth == before+1 );
            return true;
#else
            break;
#endif
        case Thing::T_Record:
            {
                Record* r = cast<Record*>(td);
                Q_ASSERT( !r->d_byValue );

                line(loc).newobj_("void class " + classRef(r) + formatMetaActuals(r)
                    + "::.ctor()"); // initializes fields incl. superclasses
            }
            return true;
        case Thing::T_Array:
            {
                Array* a = cast<Array*>(td);
                Type* td = derefed(a->d_type.data());

                int len = -1;
                if( !lengths.isEmpty() )
                {
                    Q_ASSERT( a->d_lenExpr.isNull() );
                    len = lengths.first();
                    line(loc).ldloc_(len);
                }else
                {
                    Q_ASSERT( !a->d_lenExpr.isNull() );
                    line(loc).ldc_i4(a->d_len);
                    if( td->isStructured() )
                    {
                        len = temps.buy();
                        line(loc).dup_();
                        line(loc).stloc_(len);
                    }
                }
                // here the len is on the stack, either from constant or
                line(loc).newarr_(formatType(a->d_type.data())); // must be a->d_type, not td!

                if( td->isStructured() )
                {
                    const int i = temps.buy();
                    Q_ASSERT( i >= 0 );
                    line(loc).ldc_i4(0);
                    line(loc).stloc_(i);
#if 0 // works with .Net 4.0 Windows, but neither with Mono 3 nor 5 (runtime exception because of dup (the one at loopStartLbl)
                    const int checkLenLbl = emitter->newLabel();
                    emitOpcode("br '#"+QByteArray::number(checkLenLbl)+"'",0,loc);

                    const int loopStartLbl = emitter->newLabel();
                    out << "'#" << loopStartLbl << "':" << endl;
                    emitOpcode("dup",1,loc);
                    // new array on top
                    emitOpcode("ldloc "+QByteArray::number(i),1,loc);
                    // index on top

                    if( lengths.size() > 1 )
                        emitInitializer(a->d_type.data(), false, false, loc, lengths.mid(1) );
                    else
                        emitInitializer(a->d_type.data(), false, false, loc );
                    // now the array value is on top of the stack
                    emitOpcode2("stelem ", -3, loc );
                    a->d_type->accept(this);
                    out << endl;

                    emitOpcode("ldloc "+QByteArray::number(i),1,loc);
                    emitOpcode("ldc.i4.1",1,loc);
                    emitOpcode("add",-1,loc);
                    emitOpcode("stloc "+QByteArray::number(i),-1,loc);

                    out << "'#" << checkLenLbl << "':" << endl;
                    emitOpcode("ldloc "+QByteArray::number(i),1,loc);
                    emitOpcode("ldloc "+QByteArray::number(len),1,loc);
                    emitOpcode("blt '#"+QByteArray::number(loopStartLbl)+"'",-2,loc);
#else // works with Mono 3 and .Net 4.0 Windows
                    // apparently Mono doesn't like dup after br; looks like a verifier issue
                    const int checkLenLbl = emitter->newLabel();
                    line(loc).label_(checkLenLbl);
                    line(loc).ldloc_(i);
                    line(loc).ldloc_(len);
                    const int afterLoopLbl = emitter->newLabel();
                    line(loc).bge_(afterLoopLbl);

                    line(loc).dup_();
                    // new array on top
                    line(loc).ldloc_(i);
                    // index on top

                    if( lengths.size() > 1 )
                        emitInitializer(a->d_type.data(), false, loc, lengths.mid(1) );
                    else
                        emitInitializer(a->d_type.data(), false, loc );
                    // now the array value is on top of the stack
                    line(loc).stelem_(formatType(a->d_type.data()));

                    line(loc).ldloc_(i);
                    line(loc).ldc_i4(1);
                    line(loc).add_();
                    line(loc).stloc_(i);
                    line(loc).br_(checkLenLbl);
                    line(loc).label_(afterLoopLbl);
#endif
                    temps.sell(i);
                }
                if( len >= 0 )
                    temps.sell(len);
                // leaves new array on top of stack
            }
            return true;
        default:
            Q_ASSERT(false);
        }
        return false;
    }

    void emitStackToVar( Named* me, const RowCol& loc )
    {
        switch( me->getTag() )
        {
        case Thing::T_Field:
            line(loc).stfld_(memberRef(me));
            break;
        case Thing::T_Variable:
            line(loc).stsfld_(memberRef(me));
            break;
        case Thing::T_LocalVar:
            Q_ASSERT( me->d_slotValid );
            line(loc).stloc_(me->d_slot);
            break;
        case Thing::T_Parameter:
            Q_ASSERT( me->d_slotValid );
            line(loc).starg_(me->d_slot);
            break;
        }
    }

    void emitVarToStack( Named* me, const RowCol& loc )
    {
        switch( me->getTag() )
        {
        case Thing::T_Field:
            line(loc).ldfld_(memberRef(me));
            break;
        case Thing::T_Variable:
            line(loc).ldsfld_(memberRef(me));
            break;
        case Thing::T_LocalVar:
            line(loc).ldloc_(me->d_slot);
            break;
        case Thing::T_Parameter:
            line(loc).ldarg_(me->d_slot);
            break;
        }
    }

    void emitCalcLengths( Type* t, QList<int>& lengths, const RowCol& loc )
    {
        Q_ASSERT( t );
        Array* a = 0;
        while( t->getTag() == Thing::T_Array && ( a = cast<Array*>(t) ) && a->d_lenExpr.isNull() )
        {
            // array is on the stack
            line(loc).dup_();
            line(loc).ldlen_();
            const int len = temps.buy();
            lengths.append(len);
            line(loc).stloc_(len);
            line(loc).ldc_i4(0);
            line(loc).ldelem_(formatType(a->d_type.data()));
            t = derefed(a->d_type.data() );
        }
        line(loc).pop_();
    }

    void emitInitializer( Named* me )
    {
        switch( me->getTag() )
        {
        case Thing::T_Variable:
        case Thing::T_LocalVar:
            if( emitInitializer( me->d_type.data(), false, me->d_loc ) )
                emitStackToVar( me, me->d_loc );
            break;
        case Thing::T_Parameter:
            {
                Parameter* p = cast<Parameter*>(me);
                Type* t = derefed(p->d_type.data());
                if( !p->d_var && t && t->isStructured() )
                {
                    // make a copy if a structured value is not passed by VAR or IN
                    const int tag = t->getTag();
                    QList<int> lengths;
                    if( tag == Thing::T_Array && cast<Array*>(t)->d_lenExpr.isNull() )
                    {
                        // if formal par is an open array get the length from the passed actual array
                        emitVarToStack(me, me->d_loc);
                        emitCalcLengths( t, lengths, me->d_loc );
                    }
                    emitInitializer(me->d_type.data(),false, me->d_loc, lengths );
                    // stack: array or record
                    line(me->d_loc).dup_();
                    emitVarToStack(me, me->d_loc);
                    switch(t->getTag())
                    {
                    case Thing::T_Record:
                        {
                            // stack: lhs record, lhs record, rhs record
                            Record* r = cast<Record*>(t);
                            QByteArray what = "void class " + classRef(r) + formatMetaActuals(r) + "::'#copy'(";
                            what += formatType(r);
                            if( r->d_byValue )
                                what += "&";
                            what += ")";
                            line(me->d_loc).callvirt_(what,1);
                            // stack: lhs record
                        }
                        break;
                    case Thing::T_Array:
                        {
                            // stack: lhs array, lhs array, rhs array
                            line(me->d_loc).call_(formatArrayCopierRef(cast<Array*>(t)),2);
                            // stack: lhs array
                        }
                        break;
                    default:
                        Q_ASSERT(false);
                    }
                    // store the new struct in param
                    emitStackToVar( me, me->d_loc );
                }
            }
            break;
        default:
            Q_ASSERT(false);
        }
    }

    // NOP
    void visit( NamedType* ) {Q_ASSERT( false );}
    void visit( Const* ) {Q_ASSERT( false );}
    void visit( GenericName* ) {Q_ASSERT( false );}
    void visit( BuiltIn* ) {Q_ASSERT( false );}
    void visit( Parameter* ) { Q_ASSERT( false ); }
    void visit( Import* ) { Q_ASSERT( false ); }
};

bool CilGen::translate(Module* m, IlEmitter* e, Ob::Errors* errs)
{
    Q_ASSERT( m != 0 && e != 0 );

    if( m->d_hasErrors || !m->d_isValidated ) //  not validated can happen if imports cannot be resolved
        return false;

    if( m->d_isDef )
        return true;

    ObxCilGenImp imp;
    imp.thisMod = m;
    imp.emitter = e;

    if( errs == 0 )
    {
        imp.err = new Errors();
        imp.err->setReportToConsole(true);
        imp.ownsErr = true;
    }else
    {
        imp.err = errs;
        imp.ownsErr = false;
    }
    const quint32 errCount = imp.err->getErrCount();


    m->accept(&imp);

    const bool hasErrs = ( imp.err->getErrCount() - errCount ) != 0;

    if( imp.ownsErr )
        delete imp.err;
    return hasErrs;
}

bool CilGen::generateMain(IlEmitter* e, const QByteArray& name, const QByteArray& module, const QByteArray& function)
{
    if( module.isEmpty() )
        return false;

    QByteArray mod = ObxCilGenImp::escape(name);
    QByteArrayList imports;
    imports << mod;
    e->beginModule(mod, imports,QString(),IlEmitter::ConsoleApp);
    //out << ".assembly extern mscorlib {}" << endl;

    e->beginMethod("main",false,IlEmitter::Primary);
    if( !function.isEmpty() )
        e->call_("void ['" + mod + "']'" + mod + "'::'" + function + "'()");
    else
        e->call_("void ['" + mod + "']'" + mod + "'::'ping#'()");
#if 0 // TEST
    out << "    ldstr \"this is " << name << ".main\"" << endl;
    out << "    call void [mscorlib]System.Console::WriteLine (string)" << endl;
#endif
    e->ret_();
    e->endMethod();
    e->endModule();
    return true;
}

bool CilGen::generateMain(IlEmitter* e, const QByteArray& name, const QByteArrayList& modules)
{
    Q_ASSERT( e );
    if( modules.isEmpty() )
        return false;

    QByteArrayList imports;
    foreach( const QByteArray& mod, modules )
        imports << ObxCilGenImp::escape(mod);

    e->beginModule(ObxCilGenImp::escape(name), imports,QString(),IlEmitter::ConsoleApp);
    //out << ".assembly extern mscorlib {}" << endl;

    e->beginMethod("main",false,IlEmitter::Primary);
    foreach( const QByteArray& mod, modules )
        e->call_("void ['" + mod + "']'" + mod + "'::'ping#'()");
#if 0 // TEST
    out << "    ldstr \"this is " << name << ".main\"" << endl;
    out << "    call void [mscorlib]System.Console::WriteLine (string)" << endl;
#endif
    e->ret_();
    e->endMethod();
    e->endModule();
    return true;
}

static bool copyLib( const QDir& outDir, const QByteArray& name, QTextStream& cout )
{
    QFile f( QString(":/scripts/Dll/%1.dll" ).arg(name.constData() ) );
    if( !f.open(QIODevice::ReadOnly) )
    {
        qCritical() << "unknown lib" << name;
        return false;
    }
    QFile out( outDir.absoluteFilePath(name + ".dll") );
    if( !out.open(QIODevice::WriteOnly) )
    {
        qCritical() << "cannot open for writing" << out.fileName();
        return false;
    }
    out.write( f.readAll() );
    cout << "rm \"" << name << ".dll\"" << endl;
    return true;
}

bool CilGen::translateAll(Project* pro, bool ilasm, const QString& where)
{
    Q_ASSERT( pro );
    if( where.isEmpty() )
    {
        qCritical() << "translateAll requires a path";
        return false;
    }
    QDir outDir(where);

    QByteArray buildStr;
    QTextStream bout(&buildStr);
    QByteArray clearStr;
    QTextStream cout(&clearStr);

#ifdef _MY_GENERICS_
    QList<Module*> mods = pro->getModulesToGenerate();
#else
    QList<Module*> mods = pro->getModulesToGenerate(true);
#endif
    const quint32 errCount = pro->getErrs()->getErrCount();
    QSet<Module*> generated;
    foreach( Module* m, mods )
    {
        if( m->d_synthetic )
            ; // NOP
        else if( m->d_hasErrors )
        {
            qDebug() << "terminating because of errors in" << m->d_name;
            return false;
        }else if( m->d_isDef )
        {
            // NOP, TODO
        }else
        {
#ifdef _MY_GENERICS_
            if( m->d_metaParams.isEmpty() )
            {
                QList<Module*> result;
                m->findAllInstances(result);
                result.append(m);
                foreach( Module* inst, result )
                {
                    // instances must be generated after the modules using them, otherwise we get !slotValid assertions
                    if( !generated.contains(inst) )
                    {
                        generated.insert(inst);
                        if( ilasm )
                        {
                            QFile f(outDir.absoluteFilePath(inst->getName() + ".il"));
                            if( f.open(QIODevice::WriteOnly) )
                            {
                                //qDebug() << "generating IL for" << m->getName() << "to" << f.fileName();
                                IlAsmRenderer r(&f);
                                IlEmitter e(&r);
                                CilGen::translate(inst,&e,pro->getErrs());
                                bout << "./ilasm /dll \"" << inst->getName() << ".il\"" << endl;
                                cout << "rm \"" << inst->getName() << ".il\"" << endl;
                                cout << "rm \"" << inst->getName() << ".dll\"" << endl;
                            }else
                                qCritical() << "could not open for writing" << f.fileName();
                        }else
                        {
                            PelibGen r;
                            IlEmitter e(&r);
                            CilGen::translate(inst,&e,pro->getErrs());
                            r.writeAssembler(outDir.absoluteFilePath(inst->getName() + ".il").toUtf8());
                            cout << "rm \"" << inst->getName() << ".il\"" << endl;
                            r.writeByteCode(outDir.absoluteFilePath(inst->getName() + ".dll").toUtf8());
                            cout << "rm \"" << inst->getName() << ".dll\"" << endl;
                        }
                    }
                }
            }
#else
            if( m->d_metaParams.isEmpty() || m->d_metaActuals.isEmpty() )
            {
                QFile f(outDir.absoluteFilePath(m->getName() + ".il"));
                if( f.open(QIODevice::WriteOnly) )
                {
                    qDebug() << "generating IL for" << m->getName() << "to" << f.fileName();
                    IlasmGen::translate(m,&f,pro->getErrs());
                    bout << "./ilasm /dll \"" << m->getName() << ".il\"" << endl;
                    cout << "rm \"" << m->getName() << ".il\"" << endl;
                    cout << "rm \"" << m->getName() << ".dll\"" << endl;
                }else
                    qCritical() << "could not open for writing" << f.fileName();
            }
#endif
        }
    }
    if( !mods.isEmpty() )
    {
        const QByteArray name = "Main#";
        QByteArrayList roots;
        for(int i = mods.size() - 1; i >= 0; i-- )
        {
            if( mods[i]->d_usedBy.isEmpty() )
                roots.append(mods[i]->getName());
        }
        if( roots.isEmpty() )
            roots.append(mods.last()->getName()); // shouldn't actually happenk
        if( ilasm )
        {
            QFile f(outDir.absoluteFilePath(name + ".il"));
            if( f.open(QIODevice::WriteOnly) )
            {
                IlAsmRenderer r(&f);
                IlEmitter e(&r);
                const Project::ModProc& mp = pro->getMain();
                if( mp.first.isEmpty() )
                    CilGen::generateMain(&e,name,roots);
                else
                    CilGen::generateMain(&e, name,mp.first, mp.second);
                bout << "./ilasm /exe \"" << name << ".il\"" << endl;
                cout << "rm \"" << name << ".il\"" << endl;
                cout << "rm \"" << name << ".exe\"" << endl;
            }else
                qCritical() << "could not open for writing" << f.fileName();
        }else
        {
            PelibGen r;
            IlEmitter e(&r);
            const Project::ModProc& mp = pro->getMain();
            if( mp.first.isEmpty() )
                CilGen::generateMain(&e,name,roots);
            else
                CilGen::generateMain(&e, name,mp.first, mp.second);

            r.writeAssembler(outDir.absoluteFilePath(name + ".il").toUtf8());
            cout << "rm \"" << name << ".il\"" << endl;
            r.writeByteCode(outDir.absoluteFilePath(name + ".exe").toUtf8());
            cout << "rm \"" << name << ".exe\"" << endl;
        }
        QFile json(outDir.absoluteFilePath(name + ".runtimeconfig.json"));
        if( json.open(QIODevice::WriteOnly) )
        {
            cout << "rm \"" << name << ".runtimeconfig.json\"" << endl;
            json.write("{\n\"runtimeOptions\": {\n"
                       "\"framework\": {\n"
                       "\"name\": \"Microsoft.NETCore.App\",\n"
                       "\"version\": \"3.1.0\"\n" // TODO: replace version number depending on the used CoreCLR runtime version
                       "}}}");
        }else
            qCritical() << "could not open for writing" << json.fileName();
    }

    QFile run( outDir.absoluteFilePath("run.sh") );
    if( !run.open(QIODevice::WriteOnly) )
    {
        qCritical() << "could not open for writing" << run.fileName();
        return false;
    }else
    {
        run.write("export MONO_PATH=.\n");
        run.write("./mono Main#.exe\n");
    }

    if( pro->useBuiltInOakwood() )
    {
        copyLib(outDir,"In",cout);
        copyLib(outDir,"Out",cout);
        // TODO copyLib(outDir,"Files",cout);
        copyLib(outDir,"Input",cout);
        copyLib(outDir,"Math",cout);
        copyLib(outDir,"MathL",cout);
        // TODO copyLib(outDir,"Strings",cout);
        // TODO copyLib(outDir,"Coroutines",cout);
        // TODO copyLib(outDir,"XYPlane",cout);
    }
    copyLib(outDir,"OBX.Runtime",cout);

    bout.flush();
    cout.flush();

    if( ilasm )
    {
        QFile build( outDir.absoluteFilePath("build.sh") );
        if( !build.open(QIODevice::WriteOnly) )
        {
            qCritical() << "could not open for writing" << build.fileName();
            return false;
        }else
            build.write(buildStr);
    }
    QFile clear( outDir.absoluteFilePath("clear.sh") );
    if( !clear.open(QIODevice::WriteOnly) )
    {
        qCritical() << "could not open for writing" << clear.fileName();
        return false;
    }else
        clear.write(clearStr);

    return pro->getErrs()->getErrCount() != errCount;
}
