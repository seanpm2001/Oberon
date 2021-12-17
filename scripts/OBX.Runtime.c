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

#include "OBX.Runtime.h"
#include <stdarg.h>
#ifdef OBX_USE_BOEHM_GC
#include <gc/gc.h>
#endif

inline void* OBX$ClassOf(void* inst) { return inst ? ((struct OBX$Inst*)inst)->class$ : 0; }

inline int OBX$IsSubclass( void* superClass, void* subClass )
{
    struct OBX$Class* lhs = superClass;
    struct OBX$Class* rhs = subClass;

    while( rhs && rhs != lhs )
        rhs = rhs->super$;
    return rhs == lhs;
}

inline uint32_t OBX$SetDiv( uint32_t lhs, uint32_t rhs )
{
    return ~( lhs & rhs ) & ( lhs | rhs );
}

inline int32_t OBX$Div32( int32_t a, int32_t b )
{
    // source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
    assert( b != 0 );
    if( a < 0 )
        return (a - b + 1) / b;
    else
        return a / b;
}

inline int64_t OBX$Div64( int64_t a, int64_t b )
{
    // source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
    assert( b != 0 );
    if( a < 0 )
        return (a - b + 1) / b;
    else
        return a / b;
}


inline int32_t OBX$Mod32( int32_t a, int32_t b )
{
    // source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
    assert( b != 0 );
    if (a < 0)
        return (b - 1) + (a - b + 1) % b;
    else
        return a % b;
}

inline int64_t OBX$Mod64( int64_t a, int64_t b )
{
    // source: http://lists.inf.ethz.ch/pipermail/oberon/2019/013353.html
    assert( b != 0 );
    if (a < 0)
        return (b - 1) + (a - b + 1) % b;
    else
        return a % b;
}

void* OBX$Alloc( size_t s)
{
#ifdef OBX_USE_BOEHM_GC
    return GC_MALLOC(s);
#else
    return malloc(s);
#endif
}

int OBX$StrOp( const struct OBX$Array$1* lhs, int lwide, const struct OBX$Array$1* rhs, int rwide, int op )
{
    if( !lwide && !rwide )
    {
        const char* ls = lhs->$a;
        const char* rs = rhs->$a;
        switch(op)
        {
        case 1: // ==
            return strcmp(ls,rs) == 0;
        case 2: // !=
            return strcmp(ls,rs) != 0;
        case 3: // <
            return strcmp(ls,rs) < 0;
        case 4: // <=
            return strcmp(ls,rs) <= 0;
        case 5: // >
            return strcmp(ls,rs) > 0;
        case 6: // >=
            return strcmp(ls,rs) >= 0;
        }
    }else
    {
        wchar_t* l;
        wchar_t* r;
        int bl = 0, br = 0;
        if( lwide )
            l = lhs->$a;
        else
        {
            bl = strlen(lhs->$a)+1;
            l = malloc(bl * sizeof(wchar_t));
            for(int i = 0; i < bl; i++ )
                l[i] = (uint8_t)( ((const char*)lhs->$a)[i] );
        }
        if( rwide )
            r = rhs->$a;
        else
        {
            br = strlen(rhs->$a)+1;
            r = malloc(br * sizeof(wchar_t));
            for(int i = 0; i < br; i++ )
                r[i] = (uint8_t)( ((const char*)rhs->$a)[i] );
        }
        int res = 0;
        switch(op)
        {
        case 1: // ==
            res = wcscmp(l,r) == 0;
            break;
        case 2: // !=
            res = wcscmp(l,r) != 0;
            break;
        case 3: // <
            res = wcscmp(l,r) < 0;
            break;
        case 4: // <=
            res = wcscmp(l,r) <= 0;
            break;
        case 5: // >
            res = wcscmp(l,r) > 0;
            break;
        case 6: // >=
            res = wcscmp(l,r) >= 0;
            break;
        }
        if( bl )
            free(l);
        if( br )
            free(r);
        return res;
    }
    return 0;
}

struct OBX$Array$1 OBX$StrJoin( const struct OBX$Array$1* lhs, int lwide, const struct OBX$Array$1* rhs, int rwide )
{
    // TODO: memcpy doesn't seem to work with wchar_t; make it more efficient; avoid locale dependent lib functions
    if( lwide && rwide )
    {
        const wchar_t* ls = lhs->$a;
        const wchar_t* rs = rhs->$a;
        const int lenl = wcslen(ls);
        const int lenr = wcslen(rs);
        wchar_t* str = OBX$Alloc( ( lenl + lenr + 1 ) * sizeof(wchar_t) );
        struct OBX$Array$1 res = { lenl+lenr+1,0, str };
        //memcpy(str,ls,lenl*sizeof(wchar_t));
        for(int i = 0; i < lenl; i++ )
            str[i] = ls[i];
        //memcpy(str+lenl*sizeof(wchar_t),rs,lenr*sizeof(wchar_t));
        for(int i = 0; i < lenr; i++ )
            str[i+lenl] = rs[i];
        str[lenl+lenr] = 0;
        //printf("lsw: \"%ls\"  rsw: \"%ls\"  res: \"%ls\"\n", ls, rs, str);
        return res;
    }else if( !lwide && !rwide )
    {
        const int lenl = strlen((const char*)lhs->$a);
        const int lenr = strlen((const char*)rhs->$a);
        char* str = OBX$Alloc( lenl + lenr + 1 );
        struct OBX$Array$1 res = { lenl+lenr+1,0, str };
        memcpy(str,lhs->$a,lenl);
        memcpy(str+lenl,rhs->$a,lenr);
        str[lenl+lenr] = 0;
        return res;
    }else if( lwide && !rwide )
    {
        const wchar_t* ls = lhs->$a;
        const char* rs = rhs->$a;
        const int lenl = wcslen(ls);
        const int lenr = strlen(rs);
        wchar_t* str = OBX$Alloc( ( lenl + lenr + 1 ) * sizeof(wchar_t) );
        struct OBX$Array$1 res = { lenl+lenr+1,0, str };
        //memcpy(str,ls,lenl*sizeof(wchar_t));
        for( int i = 0; i < lenl; i++ )
            str[i] = ls[i];
        for( int i = 0; i < lenr; i++ )
            str[i+lenl] = (uint8_t)rs[i];
        str[lenl+lenr] = 0;
        //printf("lsw: \"%ls\"  rsa: \"%s\"  res: \"%ls\"\n", ls, rs, str);
        return res;
    }else if( !lwide && rwide )
    {
        const char* ls = lhs->$a;
        const wchar_t* rs = rhs->$a;
        const int lenl = strlen(ls);
        const int lenr = wcslen(rs);
        wchar_t* str = OBX$Alloc( ( lenl + lenr + 1 ) * sizeof(wchar_t) );
        struct OBX$Array$1 res = { lenl+lenr+1,0, str };
        for( int i = 0; i < lenl; i++ )
            str[i] = (uint8_t)ls[i];
        for( int i = 0; i < lenr; i++ )
            str[i+lenl] = rs[i];
        //memcpy(str+lenl*sizeof(wchar_t),rs,lenr*sizeof(wchar_t));
        str[lenl+lenr] = 0;
        //printf("lsa: \"%s\"  rsw: \"%ls\"  res: \"%ls\"\n", ls, rs, str);
        return res;
    }else
        assert(0);
}

void* OBX$Copy(void* data, int len)
{
    void* res = OBX$Alloc( len );
    memcpy(res,data,len);
    return res;
}

void OBX$StrCopy(struct OBX$Array$1* lhs, int lwide, const struct OBX$Array$1* rhs, int rwide )
{
    if( lwide && rwide )
    {
        const int lenr = wcslen((const wchar_t*)rhs->$a);
        memcpy(lhs->$a,rhs->$a,(lenr+1)*sizeof(wchar_t));
    }else if( !lwide && !rwide )
    {
        const int lenr = strlen((const char*)rhs->$a);
        memcpy(lhs->$a,rhs->$a,lenr+1);
    }else if( lwide && !rwide )
    {
        const int lenr = strlen((const char*)rhs->$a);
        wchar_t* str = (wchar_t*)lhs->$a;
        for( int i = 0; i < lenr; i++ )
            str[i] = (uint8_t)(((const char*)rhs->$a)[i]);
        str[lenr] = 0;
    }else
        assert(0);
}

#define MIN(a,b) (((a)<(b))?(a):(b))

void OBX$ArrCopy(void* lhs, const void* rhs, int dims, int size )
{
    if( dims == 1 )
    {
        struct OBX$Array$1* l = lhs;
        const struct OBX$Array$1* r = rhs;
        memcpy( l->$a, r->$a, MIN(l->$1,r->$1)*size );
    }else if( dims == 2 )
    {
        struct OBX$Array$2* l = lhs;
        const struct OBX$Array$2* r = rhs;
        assert(l->$1==r->$1 && l->$2==r->$2);
        memcpy( l->$a, r->$a, l->$1*l->$2*size );
    }else if( dims == 3 )
    {
        struct OBX$Array$3* l = lhs;
        const struct OBX$Array$3* r = rhs;
        assert(l->$1==r->$1 && l->$2==r->$2 && l->$3==r->$3);
        memcpy( l->$a, r->$a, l->$1*l->$2*l->$3*size );
    }else if( dims == 4 )
    {
        struct OBX$Array$4* l = lhs;
        const struct OBX$Array$4* r = rhs;
        assert(l->$1==r->$1 && l->$2==r->$2 && l->$3==r->$3 && l->$4==r->$4);
        memcpy( l->$a, r->$a, l->$1*l->$2*l->$3*l->$4*size );
    }else if( dims == 5 )
    {
        struct OBX$Array$5* l = lhs;
        const struct OBX$Array$5* r = rhs;
        assert(l->$1==r->$1 && l->$2==r->$2 && l->$3==r->$3 && l->$4==r->$4 && l->$5==r->$5);
        memcpy( l->$a, r->$a, l->$1*l->$2*l->$3*l->$4*l->$5*size );
    }else
        assert(0);
}

void OBX$Pack32(float* x, int n)
{
    *x *= (float)powf(2, n);
}

void OBX$Unpack32(float* x, int* n)
{
    // UNPACK(4,-10) -> 1,2
    *x = frexpf(*x, n);
    *x = *x + *x;
    *n = *n - 1;
}

static uint32_t decode(const uint8_t* in, int* len )
{
    uint32_t x = 0;
    if( *in <= 0x7f )
    {
        *len = 1;
        x = *in;
    }else if( *in <= 0xdf )
    {
        *len = 2;
        x = (in[0] & 0x1f) << 6;
        x |= (in[1] & 0x3f);
    }else if( *in <= 0xef )
    {
        *len = 3;
        x = (in[0] & 0xf) << 12;
        x |= (in[1] & 0x3f) << 6;
        x |= (in[2] & 0x3f);
    }else if( *in <= 0xf7 )
    {
        *len = 4;
        x = (in[0] & 0x7) << 18;
        x |= (in[1] & 0x3f) << 12;
        x |= (in[2] & 0x3f) << 6;
        x |= (in[3] & 0x3f);
    }else
        assert(0);
    return x;
}

void* OBX$FromUtf(const char* in, int len, int wide )
{
    int i = 0;
    int n = 0;
    if( wide )
    {
        wchar_t* str = OBX$Alloc(len*sizeof(wchar_t));
        while( i < len )
        {
            const uint32_t ch = decode((const uint8_t*)in,&n);
            str[i++] = ch;
            in += n;
        }
        str[len-1] = 0;
        return str;
    }else
    {
        char* str = OBX$Alloc(len);
        while( i < len )
        {
            const uint32_t ch = decode((const uint8_t*)in,&n);
            str[i++] = (char)(uint8_t)ch;
            in += n;
        }
        str[len-1] = 0;
        return str;
    }
}

void OBX$PrintA(int ln, const char* str)
{
    const int len = strlen(str)+1;
    wchar_t* tmp = malloc(len*sizeof(wchar_t));
    for( int i = 0; i < len; i++ )
        tmp[i] = (uint8_t)str[i];
    if( ln )
    	printf("%ls\n", tmp);
    else
        printf("%ls", tmp);
            
    free(tmp);
}

uint32_t OBX$MakeSet(int count, ... )
{
	va_list ap;
	
	va_start(ap, count);

	uint32_t res = 0;
    for( int i = 0; i < count; i += 2 )
    {
        const int32_t a = va_arg(ap, int32_t);
        const int32_t b = va_arg(ap, int32_t);
        if( a >= 0 && b >= 0 )
        {
        	if( a <= b )
			    for( int j = a; j <= b; j++ )
			    	res |= 1 << j;
			// else NOP
        }else if( a >= 0 )
        {
	    	res |= 1 << a;
        }else
        	assert( 0 );
    }
    va_end(ap);
	return res;
}

// https://stackoverflow.com/a/2463888/10830469
int64_t OBX$Asr64(int64_t x, int n)
{
	if( x < 0 && n > 0 )
		return x >> n | ~(~((uint64_t)0) >> n);
	else
		return x >> n;
}

int32_t OBX$Asr32(int32_t x, int n)
{
	if( x < 0 && n > 0 )
		return x >> n | ~(~((uint32_t)0) >> n);
	else
		return x >> n;
}

int16_t OBX$Asr16(int16_t x, int n)
{
	if( x < 0 && n > 0 )
		return x >> n | ~(~((uint16_t)0) >> n);
	else
		return x >> n;
}
