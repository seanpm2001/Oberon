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

#include "ObxPelibGen.h"
#include <PeLib/PublicApi.h>
#include <QSet>
#include <QtDebug>
#include <typeinfo>
using namespace Obx;
using namespace DotNetPELib;

class SignatureLexer
{
public:
    enum TokenType { Invalid, Done, ID, QSTRING, CLASS, VALUETYPE, LBRACK, RBRACK, ARR,
                     DBLCOLON, SLASH, LPAR, RPAR, AMPERS, COMMA, DOT };
    struct Token
    {
        quint8 d_tt; // TokenType
        quint16 d_pos;
        QByteArray d_val;
        Token(TokenType t = Invalid, quint16 pos = 0, const QByteArray& val = QByteArray() ):d_tt(t),d_val(val),d_pos(pos){}
        bool isName() const { return d_tt == ID || d_tt == QSTRING; }
    };

    SignatureLexer(const QByteArray& in):d_in(in),d_off(0) {}

    Token peek()
    {
        if( d_tmp.d_tt == Invalid )
            d_tmp = nextImp();
        return d_tmp;
    }
    Token next()
    {
        if( d_tmp.d_tt != Invalid )
        {
            Token tmp = d_tmp;
            d_tmp = Token();
            return tmp;
        }else
            return nextImp();
    }
    bool next( Token& t )
    {
        t = nextImp();
        return t.d_tt != Invalid && t.d_tt != Done;
    }
    const QByteArray& text() const { return d_in; }
#if 0 // not used
    QByteArrayList params()
    {
        QByteArrayList res;
        if( peek().d_tt != LPAR )
            return res;
        int start = d_off;
        int i = start;
        int level = 0;
        bool inString = false;
        bool done = false;
        while( i < d_in.size() && !done )
        {
            switch( d_in[i] )
            {
            case ')':
                if( !inString )
                {
                    if( level == 0 )
                    {
                        if( i-start != 0 )
                            res.append( d_in.mid(start,i-start) );
                        done = true;
                    }else
                        level--;
                }
                i++;
                break;
            case '(':
                if( !inString )
                    level++;
                break;
            case '\'':
                inString = !inString;
                i++;
                break;
            case ',':
                if( !inString && level == 0 )
                {
                    res.append( d_in.mid(start,i-start) );
                    start = i+1;
                    i++;
                }
                break;
            default:
                i++;
                break;
            }
        }
        // qDebug() << d_in << res;
        return res;
    }
#endif
protected:
    Token nextImp()
    {
        char ch;
        while( get(&ch) )
        {
            if( ::isspace(ch) )
                continue;
            if( ::isalpha(ch) || ch == '_' )
                return ident(ch);
            switch(ch)
            {
            case '\'':
                return qstring();
            case '[':
                if( peek(&ch) && ch == ']' )
                {
                    get(&ch);
                    return Token(ARR,d_off-1);
                }else
                    return Token(LBRACK,d_off-1);
            case ']':
                return Token(RBRACK,d_off-1);
            case ':':
                if( get(&ch) && ch == ':' )
                    return Token(DBLCOLON,d_off-1);
                else
                    return Token(Invalid,d_off-1);
            case '/':
                return Token(SLASH,d_off-1);
            case '(':
                return Token(LPAR,d_off-1);
            case ')':
                return Token(RPAR,d_off-1);
            case '&':
                return Token(AMPERS,d_off-1);
            case ',':
                return Token(COMMA,d_off-1);
            case '.':
                if( peek(&ch) && ch == 'c' )
                    return ident(ch);
                else
                    return Token(DOT,d_off-1);
            default:
                return Token(Invalid,d_off-1);
            }
        }
        return Token(Done,d_off-1);
    }
    Token qstring()
    {
        // remove pre and post '
        const quint16 pos = d_off-1;
        char ch;
        QByteArray str;
        while( get(&ch) )
        {
            if( ch == '\'' )
                break;
            str += ch;
        }
        return Token(QSTRING,pos,str);
    }
    Token ident(char ch)
    {
        const quint16 pos = d_off-1;
        QByteArray str;
        str += ch;
        while( peek(&ch) )
        {
            if( !::isalnum(ch) && ch != '_' )
                break;
            get(&ch);
            str += ch;
        }
        if( str == "class" )
            return Token(CLASS,pos);
        if( str == "valuetype")
            return Token(VALUETYPE,pos);
        return Token(ID,pos,str);
    }
    bool get( char* ch )
    {
        if( d_off < d_in.size() )
        {
            *ch = d_in[d_off++];
            return true;
        }
        return false;
    }
    bool peek( char* ch )
    {
        if( d_off < d_in.size() )
        {
            *ch = d_in[d_off];
            return true;
        }
        return false;
    }
private:
    QByteArray d_in;
    quint32 d_off;
    Token d_tmp;
};

class SignatureParser
{
public:
    // ref      ::= typeRef | membRef
    // typeRef  ::= [ 'class' | 'valuetype' ] [ assembly ] path { '[]' } | primType { '[]' }
    // primType ::= [ 'native' ][ 'unsigned' ] ID
    // membRef  ::= typeRef [ 'class' | 'valuetype' ] [ assembly ] path '::' dottedNm [ params ]
    // assembly ::= '[' dottedNm ']'
    // path     ::= dottedNm { '/' dottedNm }
    // params   ::= '(' [ param { ',' param } ] ')'
    // param    ::= ref [ '&' ] [ name ]
    // dottedNm ::= name { '.' name }
    // name     ::= ID | QSTRING

    enum MemberHint { TypeRef, Instance, Static, Virtual };
    struct Node
    {
        typedef QMultiMap<QByteArray,Node*> Subs;

        QByteArray name;
        Resource* thing;
        Node* parent;
        Subs subs;
        Node(Node* p = 0, const QByteArray& nm = QByteArray()):thing(0),name(nm),parent(p){}
        ~Node()
        {
            Subs::const_iterator i;
            for( i = subs.begin(); i != subs.end(); ++i )
                delete i.value();
        }
    };

    SignatureParser(const QByteArray& ref, Node& r, PELib& p ):lex(ref),root(r),pe(p)
    {
        if( primitives.isEmpty() )
            primitives << "void" << "bool" << "char"
                       << "int8" << "unsigned int8" << "uint8"
                       << "int16" << "unsigned int16" << "uint16"
                       << "int32" << "unsigned int32" << "uint32"
                       << "int64" << "unsigned int64" << "uint64"
                       << "float32" << "float64"
                       << "native int" << "native unsigned int" << "native uint" << "int" << "uint"
                       << "string" << "object";
    }
    Node* parse(MemberHint hint, const QByteArray& moduleName, const QByteArray& line)
    {
        try
        {
            if( hint != TypeRef )
                return memberRef(hint);
            else
                return typeRef();
        }catch( const char* err)
        {
            qCritical() << err << "in" << moduleName.constData() << line.constData() << lex.text();
        }
        return 0;
    }
    bool isPrimitive(const QByteArray& t) const { primitives.contains(t); }

    struct Par
    {
        Node* d_type;
        QByteArray d_typeStr, d_name;
    };
    typedef QList<Par> Pars;

    static bool equalParams( MethodSignature* sig, const Pars& pars )
    {
        if( sig->getParamCount() != pars.size() )
            return false;
        for( int i = 0; i < pars.size(); i++ )
        {
            if( ::strcmp( sig->getParam(i)->typeCompare.c_str(), pars[i].d_typeStr.constData() ) != 0 )
                return false;
        }
        return true;
    }

    static Node* createMethod( Node* cls, const QByteArray& name, const Pars& pars, Node* ret, MemberHint hint )
    {
        Node* member = new Node(cls,name);
        cls->subs.insert(name,member);
        DataContainer* dc = dynamic_cast<DataContainer*>(cls->thing);
        Q_ASSERT(dc);

        Qualifiers q = Qualifiers::CIL | Qualifiers::Managed |  Qualifiers::Public;
        switch( hint )
        {
        case Static:
            q |= Qualifiers::Static;
            break;
        case Instance:
            q |= Qualifiers::Instance;
            break;
        case Virtual:
            q |= Qualifiers::Virtual;
            break;
        }

        if( name == ".ctor" || name == ".cctor" )
            q |= Qualifiers::SpecialName | Qualifiers::RTSpecialName;

        MethodSignature* sig = new MethodSignature( name.constData(), MethodSignature::Managed, dc );
        sig->ReturnType( dynamic_cast<Type*>(ret->thing) );

        for( int i = 0; i < pars.size(); i++ )
        {
            Param* p = new Param( pars[i].d_name.constData(), dynamic_cast<Type*>(pars[i].d_type->thing), i );
            p->typeCompare = pars[i].d_typeStr.constData();
            sig->AddParam( p );
        }
        Method* meth = new Method(sig,q,false);
        member->thing = meth;
        dc->Add(meth);
        return member;
    }

    static Node* findOrCreateMethod(Node* cls, const QByteArray& name, const Pars& pars, Node* ret, MemberHint hint)
    {
        QList<Node*> members = cls->subs.values( name );
        for( int i = 0; i < members.size(); i++ )
        {
            Method* meth = dynamic_cast<Method*>(members[i]->thing);
            if( meth == 0 )
                throw "this is not a method";
            MethodSignature* sig = meth->Signature();
            if( equalParams(sig,pars ) )
                return members[i];
        }
        // else
        return createMethod(cls,name,pars,ret,hint);
    }

    static void createClassFor( Node* node )
    {
        Q_ASSERT( node->thing == 0 );
        Class* cls = new Class(node->name.constData(),Qualifiers::Public,-1,-1);
        Q_ASSERT( node->parent );
        DataContainer* dc = dynamic_cast<DataContainer*>(node->parent->thing);
        Q_ASSERT(dc);
        dc->Add(cls);
        node->thing = cls;
    }

    static Node* findOrCreateField(Node* cls, const QByteArray& name, Node* type, MemberHint hint )
    {
        Node* member = cls->subs.value( name );
        if( member == 0 )
        {
            member = new Node(cls,name);
            cls->subs.insert(name,member);
            Qualifiers q = Qualifiers::Public;
            if( hint == Static )
                q |= Qualifiers::Static;
            Field* f = new Field(name.constData(),dynamic_cast<Type*>(type->thing), q);
            member->thing = f;
            Class* c = dynamic_cast<Class*>(cls->thing);
            if( c == 0 )
                throw "can only add fields to classes";
            c->Add(f);
        }else if( dynamic_cast<Field*>(member->thing) == 0 )
            throw "expecting field member";
        return member;
    }

protected:
    Node* typeRef()
    {
        SignatureLexer::Token t = lex.peek();
        if( t.d_tt == SignatureLexer::CLASS || t.d_tt == SignatureLexer::VALUETYPE )
        {
            lex.next();
            t = lex.peek();
        }
        Node* node = 0;
        Node* ass = 0;
        if( t.isName() && ( t.d_val == "native" || t.d_val == "unsigned" ) )
            node = primitiveType();
        else
        {
            if( t.d_tt == SignatureLexer::LBRACK )
                ass = assembly();
            node = path( ass ? ass : &root );
        }
        Q_ASSERT( node != 0 );
        if( node->thing == 0 )
            createClassFor(node);

        QByteArray array;
        int level = 0;
        while( lex.peek().d_tt == SignatureLexer::ARR )
        {
            lex.next();
            array += "[]";
            level++;
        }
        if( Type* primitive = dynamic_cast<Type*>(node->thing) )
        {
            if( level == 0 )
                return node;
            // else
            Node* suffix = node->subs.value(array);
            if( suffix == 0 )
            {
                suffix = new Node(node,array);
                node->subs.insert(array,suffix);
                Type* t = new Type(primitive->GetBasicType());
                t->ArrayLevel(level);
                suffix->thing = t;
            }
            return suffix;
        }else
        {
            DataContainer* dc = dynamic_cast<DataContainer*>(node->thing);
            Q_ASSERT(dc);
            Node* suffix = node->subs.value(array); // sub "" is the plain type associated with the given class
            if( suffix == 0 )
            {
                suffix = new Node(node,array);
                node->subs.insert(array,suffix);
                Type* t = new Type(dc);
                t->ArrayLevel(level);
                suffix->thing = t;
            }
            return suffix;
        }
    }
    Node* primitiveType()
    {
        QByteArrayList str;
        str += lex.next().d_val;
        SignatureLexer::Token t = lex.peek();
        if( t.isName() && t.d_val == "unsigned" )
        {
            str += lex.next().d_val;
        }
        t = lex.next();
        if( !t.isName() )
            throw "expecting ID in primitive type";
        if( !isPrimitive(t.d_val) )
            throw "expecting primitive type";
        str += t.d_val;
        return fetchPrimitive(str.join(' ') );
    }

    Node* fetchPrimitive(const QByteArray& name )
    {
        Node* p = 0;
        if( !root.subs.contains("") )
        {
            p = new Node(0,"");
            p->name = "<all primitive types>";
            root.subs.insert("", p );
        }else
            p = root.subs.value("");
        Node* t = p->subs.value(name);
        if( t == 0 )
        {
            t = new Node(p,name);
            p->subs.insert(name,t);
            if( name == "bool" )
                t->thing = new Type(Type::Bool);
            else if( name == "char" )
                t->thing = new Type(Type::Char);
            else if( name == "int8")
                t->thing = new Type(Type::i8);
            else if( name == "unsigned int8" || name == "uint8" )
                t->thing = new Type(Type::u8);
            else if( name == "int16")
                t->thing = new Type(Type::i16);
            else if( name == "unsigned int16" || name == "uint16")
                t->thing = new Type(Type::u16);
            else if( name == "int32")
                t->thing = new Type(Type::i32);
            else if( name == "unsigned int32" || name == "uint32")
                t->thing = new Type(Type::u32);
            else if( name == "int64")
                t->thing = new Type(Type::i64);
            else if( name == "unsigned int64" || name == "uint64" )
                t->thing = new Type(Type::u64);
            else if( name == "float32")
                t->thing = new Type(Type::r32);
            else if( name == "float64")
                t->thing = new Type(Type::r64);
            else if( name == "native int" || name == "int" )
                t->thing = new Type(Type::inative);
            else if( name == "native unsigned int" || name == "native uint" || name == "uint")
                t->thing = new Type(Type::unative);
            else if( name == "string")
                t->thing = new Type(Type::string);
            else if( name == "object")
                t->thing = new Type(Type::object);
            else if( name == "void" )
                t->thing = new Type(Type::Void);
            else
                throw "invalid primitive type";
        }
        return t;
    }

    Node* memberRef(MemberHint hint)
    {
        Node* type = typeRef();
        SignatureLexer::Token t = lex.peek();
        if( t.d_tt == SignatureLexer::CLASS || t.d_tt == SignatureLexer::VALUETYPE )
        {
            lex.next();
            t = lex.peek();
        }
        Node* a = 0;
        if( t.d_tt == SignatureLexer::LBRACK )
            a = assembly();
        Node* node = path( a ? a : &root );
        t = lex.next();
        if( t.d_tt != SignatureLexer::DBLCOLON )
            throw "member ref without ::";

        if( node->thing == 0 )
            createClassFor(node);
        else if( dynamic_cast<Class*>(node->thing) == 0 )
            throw "member ref must point to a class";

        t = lex.next();
        if( !t.isName() )
            throw "expecting name after '::'";

        QByteArrayList name;
        name += t.d_val;
        while( lex.peek().d_tt == SignatureLexer::DOT )
        {
            lex.next();
            t = lex.next();
            if( !t.isName() )
                throw "expecting a name after '.'";
            else
                name += t.d_val;
        }

        if( lex.peek().d_tt == SignatureLexer::LPAR )
        {
            // Method ref
            Pars pars = params();
            return findOrCreateMethod(node,name.join('.'),pars,type,hint);
        }else
        {
            // Field ref
            return findOrCreateField(node,name.join('.'),type,hint);
        }
    }

    Node* assembly()
    {
        if( lex.next().d_tt != SignatureLexer::LBRACK )
            throw "expecting '['";
        SignatureLexer::Token t = lex.next();
        QByteArrayList name;
        if( !t.isName() )
            throw "expecting name";
        name += t.d_val;
        while( lex.peek().d_tt == SignatureLexer::DOT )
        {
            lex.next();
            t = lex.next();
            if( !t.isName() )
                throw "expecting a name after '.'";
            else
                name += t.d_val;
        }
        if( lex.next().d_tt != SignatureLexer::RBRACK )
            throw "expecting ']'";
        return fetchAssembly(name.join('.'));
    }

    Node* fetchAssembly( const QByteArray& name )
    {
        Node* a = root.subs.value(name);
        if( a == 0 )
        {
            if( root.name == name )
                return &root;
            a = new Node(&root,name);
            a->thing = pe.AddExternalAssembly(name.constData());
            Q_ASSERT( a->thing );
            root.subs.insert(name,a);
        }
        return a;
    }

    Node* path(Node* scope)
    {
        QByteArrayList name;
        SignatureLexer::Token t = lex.next();
        if( !t.isName() )
            throw "expecting a path to start with a name";
        else
            name += t.d_val;

        while( lex.peek().d_tt == SignatureLexer::DOT )
        {
            lex.next();
            t = lex.next();
            if( !t.isName() )
                throw "expecting a name after '.'";
            else
                name += t.d_val;
        }

        if( name.size() == 1 && scope == &root && isPrimitive(name.first()))
            return fetchPrimitive(name.first());

        Node* node = 0;
        for( int i = 0; i < name.size() - 1; i++ )
        {
            Node* node = scope->subs.value( name[i] );
            if( node == 0 )
            {
                Namespace* ns = new Namespace(name[i].constData());
                DataContainer* dc = dynamic_cast<DataContainer*>(scope->thing);
                Q_ASSERT( dc );
                dc->Add(ns);
                node = new Node(scope,name[i]);
                node->thing = ns;
                scope->subs.insert(name[i],node);
            }
            scope = node;
        }

        node = scope->subs.value( name.last() );
        if( node == 0 )
        {
            node = new Node(scope,name.last());
            // thing is not yet known here
            scope->subs.insert(name.last(),node);
        }

        QByteArrayList nested;
        while( lex.peek().d_tt == SignatureLexer::SLASH )
        {
            lex.next();
            QByteArray dotted;
            t = lex.next();
            if( !t.isName() )
                throw "expecting a name after '/'";
            else
                dotted += t.d_val;
            while( lex.peek().d_tt == SignatureLexer::DOT )
            {
                lex.next();
                t = lex.next();
                if( !t.isName() )
                    throw "invalid dotted name after '/'";
                else
                    dotted += "." + t.d_val;
            }
            nested += dotted;
        }

        if( nested.isEmpty() )
            return node;

        // else
        // node must be a class
        if( node->thing == 0 )
            createClassFor(node);
        else if( dynamic_cast<Class*>(node->thing) == 0 )
            throw "cannot nest class in given scope";
        scope = node;

        for( int i = 0; i < nested.size(); i++ )
        {
            node = scope->subs.value( nested[i] );
            if( node == 0 )
            {
                node = new Node(scope,nested[i]);
                scope->subs.insert(nested[i],node);
                createClassFor(node);
            }else if( dynamic_cast<Class*>(node->thing) == 0 )
                throw "cannot nest class in given scope";

            scope = node;
        }
        return node;
    }

    Par param()
    {
        Par res;
        const int start = lex.peek().d_pos;
        res.d_type = typeRef();
        if( lex.peek().d_tt == SignatureLexer::AMPERS )
        {
            lex.next();
            Type* t = dynamic_cast<Type*>(res.d_type->thing);
            Q_ASSERT( t );
            if( t->GetClass() || t->ArrayLevel() )
            {
                // res.d_type is in any case a suffix Type owned by the parent node
                // or res.d_type is a suffix, make a specialized copy
                const QByteArray name = res.d_type->name + "&";
                Node* suffix = res.d_type->parent->subs.value(name);
                if( suffix == 0 )
                {
                    suffix = new Node(res.d_type->parent,name);
                    res.d_type->parent->subs.insert(name,suffix);
                    Type* tt = 0;
                    if( t->GetClass() )
                        tt = new Type(t->GetClass());
                    else
                        tt = new Type(t->GetBasicType());
                    tt->ArrayLevel(t->ArrayLevel());
                    tt->ByRef(true);
                    suffix->thing = tt;
                }
                res.d_type = suffix;
            }else
            {
                // res.d_type is the original primitive type
                Node* suffix = res.d_type->subs.value("&");
                if( suffix == 0 )
                {
                    suffix = new Node(res.d_type,"&");
                    res.d_type->subs.insert("&",suffix);
                    Type* tt = new Type(t->GetBasicType());
                    tt->ByRef(true);
                    suffix->thing = tt;
                }
                res.d_type = suffix;
            }
        }
        res.d_typeStr = lex.text().mid(start, lex.peek().d_pos-start).simplified();
        if( lex.peek().isName() )
            res.d_name = lex.next().d_val;
        return res;
    }
    Pars params()
    {
        Pars res;
        if( lex.peek().d_tt == SignatureLexer::LPAR )
            lex.next();
        else
            throw "invalid params, expecting '('";
        while( lex.peek().d_tt != SignatureLexer::RPAR )
        {
            res << param();
            while( lex.peek().d_tt == SignatureLexer::COMMA )
            {
                lex.next();
                res << param();
            }
        }
        return res;
    }

private:
    Node& root;
    PELib& pe;
    SignatureLexer lex;
    static QSet<QByteArray> primitives;
};

QSet<QByteArray> SignatureParser::primitives;

struct PelibGen::Imp : public PELib
{
    SignatureParser::Node root;
    QByteArray moduleName;
    QByteArray line; // row, col
    QList<SignatureParser::Node*> level;
    quint8 moduleKind;

    SignatureParser::Node* find(SignatureParser::MemberHint hint, const QByteArray& ref )
    {
        SignatureParser p(ref,root,*this);
        return p.parse(hint,moduleName,line);
    }

    Imp( const QByteArray& moduleName):PELib( moduleName.constData() ),moduleKind(0)
    {
        this->moduleName = moduleName;
        root.thing = WorkingAssembly();
        root.name = moduleName;
        SignatureParser::Node* mscorlib = new SignatureParser::Node(&root,"mscorlib");
        root.subs.insert("mscorlib",mscorlib);
        mscorlib->thing = MSCorLibAssembly();
        SignatureParser::Node* system = new SignatureParser::Node(mscorlib,"System");
        mscorlib->subs.insert("System",system);
        Find("[mscorlib]System",&system->thing);
        SignatureParser::Node* object = new SignatureParser::Node(system,"Object");
        system->subs.insert("Object",object);
        Find("[mscorlib]System.Object",&object->thing);
        SignatureParser::Node* value = new SignatureParser::Node(system,"ValueType");
        system->subs.insert("ValueType",value);
        Find("[mscorlib]System.ValueType",&value->thing);
        SignatureParser::Node* en = new SignatureParser::Node(system,"Enum");
        system->subs.insert("Enum",en);
        Find("[mscorlib]System.Enum",&en->thing);
    }

    void addLabelOp( Method* m, quint8 op, const QByteArray& label )
    {
        m->AddInstruction(new Instruction((Instruction::iop)op, new Operand( label.constData() ) ) );
    }

    void addTypeOp( Method* m, quint8 op, const QByteArray& typeRef )
    {
        SignatureParser::Node* type = find(SignatureParser::TypeRef,typeRef);
        Type* t = dynamic_cast<Type*>(type->thing);
        Q_ASSERT( t );
        m->AddInstruction(new Instruction((Instruction::iop)op, new Operand( new Value(t))));
    }

    void addMethodOp( Method* m, quint8 op, SignatureParser::MemberHint hint, const QByteArray& methodRef )
    {
        SignatureParser::Node* node = find(hint,methodRef);
        if( node == 0 )
            node = find(hint,methodRef); // TEST
        Method* meth = dynamic_cast<Method*>(node->thing);
        Q_ASSERT( meth );
        m->AddInstruction(new Instruction((Instruction::iop)op,
                                          new Operand( new MethodName( meth->Signature() ))));
    }

    void addFieldOp( Method* m, quint8 op, SignatureParser::MemberHint hint, const QByteArray& fieldRef )
    {
        SignatureParser::Node* node = find(hint,fieldRef);
        Field* f = dynamic_cast<Field*>(node->thing);
        Q_ASSERT( f );
        m->AddInstruction( new Instruction((Instruction::iop)op, new Operand( new FieldName(f))));
    }

    void addOperand( Method* m, quint8 op, Operand* v )
    {
        m->AddInstruction( new Instruction((Instruction::iop)op, v));
    }

    void addLocalOp( Method* m, quint8 op, const QByteArray& local )
    {
        m->AddInstruction( new Instruction((Instruction::iop)op,
                                    new Operand( m->getLocal(local.toInt() ))));
    }

    void addArgOp( Method* m, quint8 op, const QByteArray& arg )
    {
        int i = arg.toInt();
        MethodSignature* sig = m->Signature();
        // TODO: arg.0 for virtual methods is this; Q_ASSERT( i >= 0 && i < sig->ParamCount() );
        m->AddInstruction( new Instruction((Instruction::iop)op,new Operand(sig->getParam(i))));
    }
};

PelibGen::PelibGen():d_imp(0)
{
}

PelibGen::~PelibGen()
{
    if( d_imp )
        delete d_imp;
}

void PelibGen::writeByteCode(const QByteArray& filePath)
{
    Q_ASSERT( d_imp && d_imp->level.isEmpty() );
    d_imp->DumpOutputFile(filePath.constData(), d_imp->moduleKind == IlEmitter::Library ? PELib::pedll : PELib::peexe,
                          d_imp->moduleKind == IlEmitter::GuiApp );
}

void PelibGen::writeAssembler(const QByteArray& filePath)
{
    Q_ASSERT( d_imp && d_imp->level.isEmpty() );
    d_imp->DumpOutputFile(filePath.constData(), PELib::ilasm, d_imp->moduleKind == IlEmitter::GuiApp );
}

static inline QByteArray unescape( const QByteArray& name )
{
    if( name.startsWith('\'') )
    {
        Q_ASSERT( name.endsWith('\'') );
        return name.mid(1,name.size()-2);
    }else
        return name;
}

void PelibGen::beginModule(const QByteArray& moduleName, const QByteArrayList& imports, const QString& sourceFile, quint8 moduleKind)
{
    if( d_imp )
        delete d_imp;
    const QByteArray name = unescape(moduleName);
    d_imp = new Imp(name);
    d_imp->moduleKind = moduleKind;

    // TODO imports

    SignatureParser::Node* module = new SignatureParser::Node(&d_imp->root,name);
    Class* cls = new Class(name.constData(), Qualifiers::Public, -1, -1);
    d_imp->WorkingAssembly()->Add(cls);
    module->thing = cls;
    d_imp->root.subs.insert(name,module);
    d_imp->level.push_back(module);
}

static void dump(SignatureParser::Node* node, int level = 0 )
{
    qDebug() << QByteArray(level*4,' ').constData() << node->name.constData()
             << ( node->thing ? QByteArray(typeid(*node->thing).name()).mid(14) : QByteArray("nil") );
    SignatureParser::Node::Subs::const_iterator i;
    for( i = node->subs.begin(); i != node->subs.end(); ++i )
        dump(i.value(),level+1);
}

void PelibGen::endModule()
{
    Q_ASSERT( d_imp && !d_imp->level.isEmpty() );
    d_imp->level.pop_back();

    dump(&d_imp->root);
}

void PelibGen::addMethod(const IlMethod& m)
{
    Q_ASSERT( d_imp && !d_imp->level.isEmpty() );

    SignatureParser::Node* ret = d_imp->find(SignatureParser::TypeRef,
                                              m.d_retType.isEmpty() ? "void" : m.d_retType);
    const QByteArray name = unescape(m.d_name);
    SignatureParser::Pars pars;
    for( int i = 0; i < m.d_args.size(); i++ )
    {
        SignatureParser::Par p;
        p.d_type = d_imp->find(SignatureParser::TypeRef,m.d_args[i].first);
        p.d_typeStr = m.d_args[i].first.simplified();
        p.d_name = m.d_args[i].second;
        pars << p;
    }

    SignatureParser::MemberHint hint;
    switch(m.d_methodKind)
    {
    case IlEmitter::Static:
        hint = SignatureParser::Static;
        break;
    case IlEmitter::Virtual:
        hint = SignatureParser::Virtual;
        break;
    case IlEmitter::Instance:
        hint = SignatureParser::Instance;
        break;
    }

    SignatureParser::Node* meth = SignatureParser::findOrCreateMethod(d_imp->level.back(),name,pars,ret,hint);
    Method* mm = dynamic_cast<Method*>(meth->thing);
    Q_ASSERT(mm);
    mm->HasEntryPoint(m.d_methodKind == IlEmitter::Primary );

    Qualifiers q = Qualifiers::CIL | Qualifiers::Managed;
    switch( m.d_methodKind )
    {
    case IlEmitter::Static:
        q |= Qualifiers::Static;
        break;
    case IlEmitter::Primary:
        q |= Qualifiers::Static;
        break;
    case IlEmitter::Instance:
        q |= Qualifiers::Instance;
        break;
    case IlEmitter::Virtual:
        q |= Qualifiers::Virtual;
        break;
    }

    if( true ) // isPublic )
        q |= Qualifiers::Public;
    else
        q |= Qualifiers::Private;

    if( m.d_isRuntime )
        q |= Qualifiers::Runtime;

    if( name == ".ctor" )
    {
        q |= Qualifiers::SpecialName | Qualifiers::RTSpecialName;
        q |= Qualifiers::Instance;
    }else if( name == ".cctor" )
    {
        q |= Qualifiers::SpecialName | Qualifiers::RTSpecialName;
        q |= Qualifiers::Static;
    }

    mm->Flags() = q;
    mm->MaxStack(m.d_stackDepth);

    Q_ASSERT( mm->size() == 0 ); // no locals yet
    for( int i = 0; i < m.d_locals.size(); i++ )
    {
        SignatureParser::Node* type = d_imp->find(SignatureParser::TypeRef,m.d_locals[i].first);
        Local* loc = new Local(m.d_locals[i].second.constData(), dynamic_cast<Type*>(type->thing) );
        mm->AddLocal(loc);
    }

    for( int i = 0; i < m.d_body.size(); i++ )
    {
        const IlOperation& op = m.d_body[i];
        switch( op.d_ilop )
        {
        case IL_invalid:
            break;
        case IL_label:
            d_imp->addLabelOp(mm,op.d_ilop,op.d_arg);
            break;
        case IL_line:
            d_imp->line = op.d_arg;
            break;
        case IL_brinst:
        case IL_brtrue:
        case IL_brzero:
        case IL_brnull:
        case IL_brfalse:
        case IL_br:
        case IL_bne_un:
        case IL_blt:
        case IL_blt_un:
        case IL_ble:
        case IL_ble_un:
        case IL_bgt:
        case IL_bgt_un:
        case IL_bge:
        case IL_bge_un:
        case IL_beq:
            d_imp->addLabelOp(mm,op.d_ilop,op.d_arg);
            break;
        case IL_call:
            d_imp->addMethodOp(mm,op.d_ilop,op.d_flags ?
                                   SignatureParser::Instance : SignatureParser::Static,op.d_arg);
            break;
        case IL_callvirt:
            d_imp->addMethodOp(mm,op.d_ilop,SignatureParser::Virtual,op.d_arg);
            break;
        case IL_newobj:
            d_imp->addMethodOp(mm,op.d_ilop,SignatureParser::Instance,op.d_arg);
            break;
        case IL_box:
        case IL_castclass:
        case IL_initobj:
        case IL_isinst:
        case IL_ldelem:
        case IL_ldelema:
        case IL_ldobj:
        case IL_newarr:
        case IL_stelem:
        case IL_stobj:
        case IL_unbox:
            d_imp->addTypeOp(mm,op.d_ilop,op.d_arg);
            break;
        case IL_ldfld:
        case IL_ldflda:
        case IL_stfld:
            // TODO: PElib doesn't seem to handle the argument offset in the presence of an implicit this parameter.
            d_imp->addFieldOp(mm,op.d_ilop,SignatureParser::Instance,op.d_arg);
            break;
        case IL_ldsfld:
        case IL_ldsflda:
        case IL_stsfld:
            d_imp->addFieldOp(mm,op.d_ilop,SignatureParser::Static,op.d_arg);
            break;
        case IL_ldftn:
            d_imp->addMethodOp(mm,op.d_ilop,SignatureParser::Static,op.d_arg); // TODO: static or instance?
            break;
        case IL_ldvirtftn:
            d_imp->addMethodOp(mm,op.d_ilop,SignatureParser::Virtual,op.d_arg);
            break;
        case IL_ldstr:
            {
                std::string str = op.d_arg.mid(1,op.d_arg.size()-2-1).constData(); // remove "" and chop '0', only '\' remains
                str[ str.size() - 1 ] = 0;
                mm->AddInstruction(
                        new Instruction(Instruction::i_ldstr, new Operand(str,true)));
            }
            break;
        case IL_ldc_r8:
            d_imp->addOperand(mm,op.d_ilop, new Operand( op.d_arg.toDouble(), Operand::r8));
            break;
        case IL_ldc_r4:
            d_imp->addOperand(mm,op.d_ilop, new Operand( op.d_arg.toDouble(), Operand::r4));
            break;
        case IL_ldc_i8:
            d_imp->addOperand(mm,op.d_ilop, new Operand( op.d_arg.toLongLong(), Operand::i8));
            break;
        case IL_ldc_i4:
        case IL_ldc_i4_s:
            d_imp->addOperand(mm,op.d_ilop, new Operand( op.d_arg.toInt(), Operand::i32));
            break;
        case IL_stloc:
        case IL_stloc_s:
        case IL_ldloca:
        case IL_ldloca_s:
        case IL_ldloc:
        case IL_ldloc_s:
            d_imp->addLocalOp(mm,op.d_ilop,op.d_arg);
            break;
        case IL_starg:
        case IL_starg_s:
        case IL_ldarga:
        case IL_ldarga_s:
        case IL_ldarg:
        case IL_ldarg_s:
            d_imp->addArgOp(mm,op.d_ilop,op.d_arg);
            break;
        default:
            // no argument ops
            mm->AddInstruction(new Instruction((Instruction::iop)op.d_ilop));
            break;
        }
    }
    mm->Optimize(*d_imp);
}

void PelibGen::beginClass(const QByteArray& className, bool isPublic, const QByteArray& superClassRef)
{
    Q_ASSERT( d_imp && !d_imp->level.isEmpty() );
    const QByteArray name = unescape(className);
    SignatureParser::Node* me = d_imp->level.back()->subs.value(name);
    Class* cls = 0;
    if( me == 0 )
    {
        me = new SignatureParser::Node(d_imp->level.back(),name);
        cls = new Class(name.constData(), Qualifiers::Public, -1, -1);
        DataContainer* dc = dynamic_cast<DataContainer*>(d_imp->level.back()->thing);
        Q_ASSERT(dc);
        dc->Add(cls);
        me->thing = cls;
        d_imp->level.back()->subs.insert(name,me);
    }else
        cls = dynamic_cast<Class*>(me->thing);
    Q_ASSERT(cls);
    if( !superClassRef.isEmpty() && cls->Extends() == 0 )
    {
        SignatureParser::Node* super = d_imp->find(SignatureParser::TypeRef,superClassRef);
        Type* t = dynamic_cast<Type*>(super->thing);
        Q_ASSERT( t );
        Class* scls = dynamic_cast<Class*>(t->GetClass());
        Q_ASSERT( scls );
        cls->Extends(scls);
    }
    d_imp->level.push_back(me);
}

void PelibGen::endClass()
{
    Q_ASSERT( d_imp && !d_imp->level.isEmpty() );
    d_imp->level.pop_back();
}

void PelibGen::addField(const QByteArray& fieldName, const QByteArray& typeRef, bool isPublic, bool isStatic)
{
    SignatureParser::MemberHint hint;
    if( isStatic )
        hint = SignatureParser::Static;
    else
        hint = SignatureParser::Instance;

    SignatureParser::Node* field = SignatureParser::findOrCreateField(d_imp->level.back(),unescape(fieldName),
                                       d_imp->find(SignatureParser::TypeRef,typeRef), hint);
}


