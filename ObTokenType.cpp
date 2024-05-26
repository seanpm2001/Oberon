// This file was automatically generated by EbnfStudio; don't modify it!
#include "ObTokenType.h"

namespace Ob {
	const char* tokenTypeString( int r ) {
		switch(r) {
			case Tok_Invalid: return "<invalid>";
			case Tok_Hash: return "#";
			case Tok_Dlr: return "$";
			case Tok_Amp: return "&";
			case Tok_Lpar: return "(";
			case Tok_Latt: return "(*";
			case Tok_Rpar: return ")";
			case Tok_Star: return "*";
			case Tok_Ratt: return "*)";
			case Tok_StarGt: return "*>";
			case Tok_Plus: return "+";
			case Tok_Comma: return ",";
			case Tok_Minus: return "-";
			case Tok_Dot: return ".";
			case Tok_2Dot: return "..";
			case Tok_Slash: return "/";
			case Tok_2Slash: return "//";
			case Tok_Colon: return ":";
			case Tok_ColonEq: return ":=";
			case Tok_Semi: return ";";
			case Tok_Lt: return "<";
			case Tok_LtStar: return "<*";
			case Tok_Leq: return "<=";
			case Tok_Eq: return "=";
			case Tok_Gt: return ">";
			case Tok_Geq: return ">=";
			case Tok_Lbrack: return "[";
			case Tok_Rbrack: return "]";
			case Tok_Hat: return "^";
			case Tok_Lbrace: return "{";
			case Tok_Bar: return "|";
			case Tok_Rbrace: return "}";
			case Tok_Tilde: return "~";
			case Tok_ARRAY: return "ARRAY";
			case Tok_BEGIN: return "BEGIN";
			case Tok_BY: return "BY";
			case Tok_CARRAY: return "CARRAY";
			case Tok_CASE: return "CASE";
			case Tok_CONST: return "CONST";
			case Tok_CPOINTER: return "CPOINTER";
			case Tok_CSTRUCT: return "CSTRUCT";
			case Tok_CUNION: return "CUNION";
			case Tok_DEFINITION: return "DEFINITION";
			case Tok_DIV: return "DIV";
			case Tok_DO: return "DO";
			case Tok_ELSE: return "ELSE";
			case Tok_ELSIF: return "ELSIF";
			case Tok_END: return "END";
			case Tok_EXIT: return "EXIT";
			case Tok_FALSE: return "FALSE";
			case Tok_FOR: return "FOR";
			case Tok_IF: return "IF";
			case Tok_IMPORT: return "IMPORT";
			case Tok_IN: return "IN";
			case Tok_IS: return "IS";
			case Tok_LOOP: return "LOOP";
			case Tok_MOD: return "MOD";
			case Tok_MODULE: return "MODULE";
			case Tok_NIL: return "NIL";
			case Tok_OF: return "OF";
			case Tok_OR: return "OR";
			case Tok_POINTER: return "POINTER";
			case Tok_PROC: return "PROC";
			case Tok_PROCEDURE: return "PROCEDURE";
			case Tok_RECORD: return "RECORD";
			case Tok_REPEAT: return "REPEAT";
			case Tok_RETURN: return "RETURN";
			case Tok_THEN: return "THEN";
			case Tok_TO: return "TO";
			case Tok_TRUE: return "TRUE";
			case Tok_TYPE: return "TYPE";
			case Tok_UNSAFE: return "UNSAFE";
			case Tok_UNTIL: return "UNTIL";
			case Tok_VAR: return "VAR";
			case Tok_WEAK: return "WEAK";
			case Tok_WHILE: return "WHILE";
			case Tok_WITH: return "WITH";
			case Tok_ident: return "ident";
			case Tok_integer: return "integer";
			case Tok_real: return "real";
			case Tok_string: return "string";
			case Tok_hexchar: return "hexchar";
			case Tok_hexstring: return "hexstring";
			case Tok_Comment: return "Comment";
			case Tok_Eof: return "<eof>";
			default: return "";
		}
	}
	const char* tokenTypeName( int r ) {
		switch(r) {
			case Tok_Invalid: return "Tok_Invalid";
			case Tok_Hash: return "Tok_Hash";
			case Tok_Dlr: return "Tok_Dlr";
			case Tok_Amp: return "Tok_Amp";
			case Tok_Lpar: return "Tok_Lpar";
			case Tok_Latt: return "Tok_Latt";
			case Tok_Rpar: return "Tok_Rpar";
			case Tok_Star: return "Tok_Star";
			case Tok_Ratt: return "Tok_Ratt";
			case Tok_StarGt: return "Tok_StarGt";
			case Tok_Plus: return "Tok_Plus";
			case Tok_Comma: return "Tok_Comma";
			case Tok_Minus: return "Tok_Minus";
			case Tok_Dot: return "Tok_Dot";
			case Tok_2Dot: return "Tok_2Dot";
			case Tok_Slash: return "Tok_Slash";
			case Tok_2Slash: return "Tok_2Slash";
			case Tok_Colon: return "Tok_Colon";
			case Tok_ColonEq: return "Tok_ColonEq";
			case Tok_Semi: return "Tok_Semi";
			case Tok_Lt: return "Tok_Lt";
			case Tok_LtStar: return "Tok_LtStar";
			case Tok_Leq: return "Tok_Leq";
			case Tok_Eq: return "Tok_Eq";
			case Tok_Gt: return "Tok_Gt";
			case Tok_Geq: return "Tok_Geq";
			case Tok_Lbrack: return "Tok_Lbrack";
			case Tok_Rbrack: return "Tok_Rbrack";
			case Tok_Hat: return "Tok_Hat";
			case Tok_Lbrace: return "Tok_Lbrace";
			case Tok_Bar: return "Tok_Bar";
			case Tok_Rbrace: return "Tok_Rbrace";
			case Tok_Tilde: return "Tok_Tilde";
			case Tok_ARRAY: return "Tok_ARRAY";
			case Tok_BEGIN: return "Tok_BEGIN";
			case Tok_BY: return "Tok_BY";
			case Tok_CARRAY: return "Tok_CARRAY";
			case Tok_CASE: return "Tok_CASE";
			case Tok_CONST: return "Tok_CONST";
			case Tok_CPOINTER: return "Tok_CPOINTER";
			case Tok_CSTRUCT: return "Tok_CSTRUCT";
			case Tok_CUNION: return "Tok_CUNION";
			case Tok_DEFINITION: return "Tok_DEFINITION";
			case Tok_DIV: return "Tok_DIV";
			case Tok_DO: return "Tok_DO";
			case Tok_ELSE: return "Tok_ELSE";
			case Tok_ELSIF: return "Tok_ELSIF";
			case Tok_END: return "Tok_END";
			case Tok_EXIT: return "Tok_EXIT";
			case Tok_FALSE: return "Tok_FALSE";
			case Tok_FOR: return "Tok_FOR";
			case Tok_IF: return "Tok_IF";
			case Tok_IMPORT: return "Tok_IMPORT";
			case Tok_IN: return "Tok_IN";
			case Tok_IS: return "Tok_IS";
			case Tok_LOOP: return "Tok_LOOP";
			case Tok_MOD: return "Tok_MOD";
			case Tok_MODULE: return "Tok_MODULE";
			case Tok_NIL: return "Tok_NIL";
			case Tok_OF: return "Tok_OF";
			case Tok_OR: return "Tok_OR";
			case Tok_POINTER: return "Tok_POINTER";
			case Tok_PROC: return "Tok_PROC";
			case Tok_PROCEDURE: return "Tok_PROCEDURE";
			case Tok_RECORD: return "Tok_RECORD";
			case Tok_REPEAT: return "Tok_REPEAT";
			case Tok_RETURN: return "Tok_RETURN";
			case Tok_THEN: return "Tok_THEN";
			case Tok_TO: return "Tok_TO";
			case Tok_TRUE: return "Tok_TRUE";
			case Tok_TYPE: return "Tok_TYPE";
			case Tok_UNSAFE: return "Tok_UNSAFE";
			case Tok_UNTIL: return "Tok_UNTIL";
			case Tok_VAR: return "Tok_VAR";
			case Tok_WEAK: return "Tok_WEAK";
			case Tok_WHILE: return "Tok_WHILE";
			case Tok_WITH: return "Tok_WITH";
			case Tok_ident: return "Tok_ident";
			case Tok_integer: return "Tok_integer";
			case Tok_real: return "Tok_real";
			case Tok_string: return "Tok_string";
			case Tok_hexchar: return "Tok_hexchar";
			case Tok_hexstring: return "Tok_hexstring";
			case Tok_Comment: return "Tok_Comment";
			case Tok_Eof: return "Tok_Eof";
			default: return "";
		}
	}
	bool tokenTypeIsLiteral( int r ) {
		return r > TT_Literals && r < TT_Keywords;
	}
	bool tokenTypeIsKeyword( int r ) {
		return r > TT_Keywords && r < TT_Specials;
	}
	bool tokenTypeIsSpecial( int r ) {
		return r > TT_Specials && r < TT_Max;
	}
	static inline char at( const char* str, quint32 len, int i ){
		return ( i < len ? str[i] : 0 );
	}
	TokenType tokenTypeFromString( const QByteArray& str, int* pos ) {
		return tokenTypeFromString(str.constData(),str.size(),pos);
	}
	TokenType tokenTypeFromString( const char* str, quint32 len, int* pos ) {
		int i = ( pos != 0 ? *pos: 0 );
		TokenType res = Tok_Invalid;
		switch( at(str,len,i) ){
		case '#':
			res = Tok_Hash; i += 1;
			break;
		case '$':
			res = Tok_Dlr; i += 1;
			break;
		case '&':
			res = Tok_Amp; i += 1;
			break;
		case '(':
			if( at(str,len,i+1) == '*' ){
				res = Tok_Latt; i += 2;
			} else {
				res = Tok_Lpar; i += 1;
			}
			break;
		case ')':
			res = Tok_Rpar; i += 1;
			break;
		case '*':
			switch( at(str,len,i+1) ){
			case ')':
				res = Tok_Ratt; i += 2;
				break;
			case '>':
				res = Tok_StarGt; i += 2;
				break;
			default:
				res = Tok_Star; i += 1;
				break;
			}
			break;
		case '+':
			res = Tok_Plus; i += 1;
			break;
		case ',':
			res = Tok_Comma; i += 1;
			break;
		case '-':
			res = Tok_Minus; i += 1;
			break;
		case '.':
			if( at(str,len,i+1) == '.' ){
				res = Tok_2Dot; i += 2;
			} else {
				res = Tok_Dot; i += 1;
			}
			break;
		case '/':
			if( at(str,len,i+1) == '/' ){
				res = Tok_2Slash; i += 2;
			} else {
				res = Tok_Slash; i += 1;
			}
			break;
		case ':':
			if( at(str,len,i+1) == '=' ){
				res = Tok_ColonEq; i += 2;
			} else {
				res = Tok_Colon; i += 1;
			}
			break;
		case ';':
			res = Tok_Semi; i += 1;
			break;
		case '<':
			switch( at(str,len,i+1) ){
			case '*':
				res = Tok_LtStar; i += 2;
				break;
			case '=':
				res = Tok_Leq; i += 2;
				break;
			default:
				res = Tok_Lt; i += 1;
				break;
			}
			break;
		case '=':
			res = Tok_Eq; i += 1;
			break;
		case '>':
			if( at(str,len,i+1) == '=' ){
				res = Tok_Geq; i += 2;
			} else {
				res = Tok_Gt; i += 1;
			}
			break;
		case 'A':
			if( at(str,len,i+1) == 'R' ){
				if( at(str,len,i+2) == 'R' ){
					if( at(str,len,i+3) == 'A' ){
						if( at(str,len,i+4) == 'Y' ){
							res = Tok_ARRAY; i += 5;
						}
					}
				}
			}
			break;
		case 'B':
			switch( at(str,len,i+1) ){
			case 'E':
				if( at(str,len,i+2) == 'G' ){
					if( at(str,len,i+3) == 'I' ){
						if( at(str,len,i+4) == 'N' ){
							res = Tok_BEGIN; i += 5;
						}
					}
				}
				break;
			case 'Y':
				res = Tok_BY; i += 2;
				break;
			}
			break;
		case 'C':
			switch( at(str,len,i+1) ){
			case 'A':
				switch( at(str,len,i+2) ){
				case 'R':
					if( at(str,len,i+3) == 'R' ){
						if( at(str,len,i+4) == 'A' ){
							if( at(str,len,i+5) == 'Y' ){
								res = Tok_CARRAY; i += 6;
							}
						}
					}
					break;
				case 'S':
					if( at(str,len,i+3) == 'E' ){
						res = Tok_CASE; i += 4;
					}
					break;
				}
				break;
			case 'O':
				if( at(str,len,i+2) == 'N' ){
					if( at(str,len,i+3) == 'S' ){
						if( at(str,len,i+4) == 'T' ){
							res = Tok_CONST; i += 5;
						}
					}
				}
				break;
			case 'P':
				if( at(str,len,i+2) == 'O' ){
					if( at(str,len,i+3) == 'I' ){
						if( at(str,len,i+4) == 'N' ){
							if( at(str,len,i+5) == 'T' ){
								if( at(str,len,i+6) == 'E' ){
									if( at(str,len,i+7) == 'R' ){
										res = Tok_CPOINTER; i += 8;
									}
								}
							}
						}
					}
				}
				break;
			case 'S':
				if( at(str,len,i+2) == 'T' ){
					if( at(str,len,i+3) == 'R' ){
						if( at(str,len,i+4) == 'U' ){
							if( at(str,len,i+5) == 'C' ){
								if( at(str,len,i+6) == 'T' ){
									res = Tok_CSTRUCT; i += 7;
								}
							}
						}
					}
				}
				break;
			case 'U':
				if( at(str,len,i+2) == 'N' ){
					if( at(str,len,i+3) == 'I' ){
						if( at(str,len,i+4) == 'O' ){
							if( at(str,len,i+5) == 'N' ){
								res = Tok_CUNION; i += 6;
							}
						}
					}
				}
				break;
			}
			break;
		case 'D':
			switch( at(str,len,i+1) ){
			case 'E':
				if( at(str,len,i+2) == 'F' ){
					if( at(str,len,i+3) == 'I' ){
						if( at(str,len,i+4) == 'N' ){
							if( at(str,len,i+5) == 'I' ){
								if( at(str,len,i+6) == 'T' ){
									if( at(str,len,i+7) == 'I' ){
										if( at(str,len,i+8) == 'O' ){
											if( at(str,len,i+9) == 'N' ){
												res = Tok_DEFINITION; i += 10;
											}
										}
									}
								}
							}
						}
					}
				}
				break;
			case 'I':
				if( at(str,len,i+2) == 'V' ){
					res = Tok_DIV; i += 3;
				}
				break;
			case 'O':
				res = Tok_DO; i += 2;
				break;
			}
			break;
		case 'E':
			switch( at(str,len,i+1) ){
			case 'L':
				if( at(str,len,i+2) == 'S' ){
					switch( at(str,len,i+3) ){
					case 'E':
						res = Tok_ELSE; i += 4;
						break;
					case 'I':
						if( at(str,len,i+4) == 'F' ){
							res = Tok_ELSIF; i += 5;
						}
						break;
					}
				}
				break;
			case 'N':
				if( at(str,len,i+2) == 'D' ){
					res = Tok_END; i += 3;
				}
				break;
			case 'X':
				if( at(str,len,i+2) == 'I' ){
					if( at(str,len,i+3) == 'T' ){
						res = Tok_EXIT; i += 4;
					}
				}
				break;
			}
			break;
		case 'F':
			switch( at(str,len,i+1) ){
			case 'A':
				if( at(str,len,i+2) == 'L' ){
					if( at(str,len,i+3) == 'S' ){
						if( at(str,len,i+4) == 'E' ){
							res = Tok_FALSE; i += 5;
						}
					}
				}
				break;
			case 'O':
				if( at(str,len,i+2) == 'R' ){
					res = Tok_FOR; i += 3;
				}
				break;
			}
			break;
		case 'I':
			switch( at(str,len,i+1) ){
			case 'F':
				res = Tok_IF; i += 2;
				break;
			case 'M':
				if( at(str,len,i+2) == 'P' ){
					if( at(str,len,i+3) == 'O' ){
						if( at(str,len,i+4) == 'R' ){
							if( at(str,len,i+5) == 'T' ){
								res = Tok_IMPORT; i += 6;
							}
						}
					}
				}
				break;
			case 'N':
				res = Tok_IN; i += 2;
				break;
			case 'S':
				res = Tok_IS; i += 2;
				break;
			}
			break;
		case 'L':
			if( at(str,len,i+1) == 'O' ){
				if( at(str,len,i+2) == 'O' ){
					if( at(str,len,i+3) == 'P' ){
						res = Tok_LOOP; i += 4;
					}
				}
			}
			break;
		case 'M':
			if( at(str,len,i+1) == 'O' ){
				if( at(str,len,i+2) == 'D' ){
					if( at(str,len,i+3) == 'U' ){
						if( at(str,len,i+4) == 'L' ){
							if( at(str,len,i+5) == 'E' ){
								res = Tok_MODULE; i += 6;
							}
						}
					} else {
						res = Tok_MOD; i += 3;
					}
				}
			}
			break;
		case 'N':
			if( at(str,len,i+1) == 'I' ){
				if( at(str,len,i+2) == 'L' ){
					res = Tok_NIL; i += 3;
				}
			}
			break;
		case 'O':
			switch( at(str,len,i+1) ){
			case 'F':
				res = Tok_OF; i += 2;
				break;
			case 'R':
				res = Tok_OR; i += 2;
				break;
			}
			break;
		case 'P':
			switch( at(str,len,i+1) ){
			case 'O':
				if( at(str,len,i+2) == 'I' ){
					if( at(str,len,i+3) == 'N' ){
						if( at(str,len,i+4) == 'T' ){
							if( at(str,len,i+5) == 'E' ){
								if( at(str,len,i+6) == 'R' ){
									res = Tok_POINTER; i += 7;
								}
							}
						}
					}
				}
				break;
			case 'R':
				if( at(str,len,i+2) == 'O' ){
					if( at(str,len,i+3) == 'C' ){
						if( at(str,len,i+4) == 'E' ){
							if( at(str,len,i+5) == 'D' ){
								if( at(str,len,i+6) == 'U' ){
									if( at(str,len,i+7) == 'R' ){
										if( at(str,len,i+8) == 'E' ){
											res = Tok_PROCEDURE; i += 9;
										}
									}
								}
							}
						} else {
							res = Tok_PROC; i += 4;
						}
					}
				}
				break;
			}
			break;
		case 'R':
			if( at(str,len,i+1) == 'E' ){
				switch( at(str,len,i+2) ){
				case 'C':
					if( at(str,len,i+3) == 'O' ){
						if( at(str,len,i+4) == 'R' ){
							if( at(str,len,i+5) == 'D' ){
								res = Tok_RECORD; i += 6;
							}
						}
					}
					break;
				case 'P':
					if( at(str,len,i+3) == 'E' ){
						if( at(str,len,i+4) == 'A' ){
							if( at(str,len,i+5) == 'T' ){
								res = Tok_REPEAT; i += 6;
							}
						}
					}
					break;
				case 'T':
					if( at(str,len,i+3) == 'U' ){
						if( at(str,len,i+4) == 'R' ){
							if( at(str,len,i+5) == 'N' ){
								res = Tok_RETURN; i += 6;
							}
						}
					}
					break;
				}
			}
			break;
		case 'T':
			switch( at(str,len,i+1) ){
			case 'H':
				if( at(str,len,i+2) == 'E' ){
					if( at(str,len,i+3) == 'N' ){
						res = Tok_THEN; i += 4;
					}
				}
				break;
			case 'O':
				res = Tok_TO; i += 2;
				break;
			case 'R':
				if( at(str,len,i+2) == 'U' ){
					if( at(str,len,i+3) == 'E' ){
						res = Tok_TRUE; i += 4;
					}
				}
				break;
			case 'Y':
				if( at(str,len,i+2) == 'P' ){
					if( at(str,len,i+3) == 'E' ){
						res = Tok_TYPE; i += 4;
					}
				}
				break;
			}
			break;
		case 'U':
			if( at(str,len,i+1) == 'N' ){
				switch( at(str,len,i+2) ){
				case 'S':
					if( at(str,len,i+3) == 'A' ){
						if( at(str,len,i+4) == 'F' ){
							if( at(str,len,i+5) == 'E' ){
								res = Tok_UNSAFE; i += 6;
							}
						}
					}
					break;
				case 'T':
					if( at(str,len,i+3) == 'I' ){
						if( at(str,len,i+4) == 'L' ){
							res = Tok_UNTIL; i += 5;
						}
					}
					break;
				}
			}
			break;
		case 'V':
			if( at(str,len,i+1) == 'A' ){
				if( at(str,len,i+2) == 'R' ){
					res = Tok_VAR; i += 3;
				}
			}
			break;
		case 'W':
			switch( at(str,len,i+1) ){
			case 'E':
				if( at(str,len,i+2) == 'A' ){
					if( at(str,len,i+3) == 'K' ){
						res = Tok_WEAK; i += 4;
					}
				}
				break;
			case 'H':
				if( at(str,len,i+2) == 'I' ){
					if( at(str,len,i+3) == 'L' ){
						if( at(str,len,i+4) == 'E' ){
							res = Tok_WHILE; i += 5;
						}
					}
				}
				break;
			case 'I':
				if( at(str,len,i+2) == 'T' ){
					if( at(str,len,i+3) == 'H' ){
						res = Tok_WITH; i += 4;
					}
				}
				break;
			}
			break;
		case '[':
			res = Tok_Lbrack; i += 1;
			break;
		case ']':
			res = Tok_Rbrack; i += 1;
			break;
		case '^':
			res = Tok_Hat; i += 1;
			break;
		case '{':
			res = Tok_Lbrace; i += 1;
			break;
		case '|':
			res = Tok_Bar; i += 1;
			break;
		case '}':
			res = Tok_Rbrace; i += 1;
			break;
		case '~':
			res = Tok_Tilde; i += 1;
			break;
		}
		if(pos) *pos = i;
		return res;
	}
}
