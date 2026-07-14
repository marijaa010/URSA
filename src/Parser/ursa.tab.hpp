/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_URSA_TAB_HPP_INCLUDED
# define YY_YY_URSA_TAB_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    INTEGER = 258,                 /* INTEGER  */
    BOOLEAN = 259,                 /* BOOLEAN  */
    BOOLEAN_VARIABLE = 260,        /* BOOLEAN_VARIABLE  */
    INTEGER_VARIABLE = 261,        /* INTEGER_VARIABLE  */
    BOOLEAN_ARRAY = 262,           /* BOOLEAN_ARRAY  */
    INTEGER_ARRAY = 263,           /* INTEGER_ARRAY  */
    PROCEDURE_ID = 264,            /* PROCEDURE_ID  */
    FOR = 265,                     /* FOR  */
    WHILE = 266,                   /* WHILE  */
    IF = 267,                      /* IF  */
    PRINT = 268,                   /* PRINT  */
    PRINTB = 269,                  /* PRINTB  */
    PRINTX = 270,                  /* PRINTX  */
    MINIMIZE = 271,                /* MINIMIZE  */
    MAXIMIZE = 272,                /* MAXIMIZE  */
    ASSERT = 273,                  /* ASSERT  */
    ASSERTA = 274,                 /* ASSERTA  */
    LIST = 275,                    /* LIST  */
    CLEAR = 276,                   /* CLEAR  */
    HALT = 277,                    /* HALT  */
    PROCEDURE = 278,               /* PROCEDURE  */
    CALL = 279,                    /* CALL  */
    IFX = 280,                     /* IFX  */
    ELSE = 281,                    /* ELSE  */
    PLUSEQ = 282,                  /* PLUSEQ  */
    MINUSEQ = 283,                 /* MINUSEQ  */
    MULTEQ = 284,                  /* MULTEQ  */
    DIVEQ = 285,                   /* DIVEQ  */
    ANDEQ = 286,                   /* ANDEQ  */
    OREQ = 287,                    /* OREQ  */
    XOREQ = 288,                   /* XOREQ  */
    LSHIFTEQ = 289,                /* LSHIFTEQ  */
    RSHIFTEQ = 290,                /* RSHIFTEQ  */
    BITWISEANDEQ = 291,            /* BITWISEANDEQ  */
    BITWISEOREQ = 292,             /* BITWISEOREQ  */
    BITWISEXOREQ = 293,            /* BITWISEXOREQ  */
    LOGICALXOR = 294,              /* LOGICALXOR  */
    LOGICALOR = 295,               /* LOGICALOR  */
    LOGICALAND = 296,              /* LOGICALAND  */
    GE = 297,                      /* GE  */
    LE = 298,                      /* LE  */
    EQ = 299,                      /* EQ  */
    NE = 300,                      /* NE  */
    LSHIFT = 301,                  /* LSHIFT  */
    RSHIFT = 302,                  /* RSHIFT  */
    PLUSPLUS = 303,                /* PLUSPLUS  */
    MINUSMINUS = 304,              /* MINUSMINUS  */
    UMINUS = 305,                  /* UMINUS  */
    ITE = 306,                     /* ITE  */
    BOOL2NUM = 307,                /* BOOL2NUM  */
    NUM2BOOL = 308,                /* NUM2BOOL  */
    SGN = 309                      /* SGN  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 22 "Parser/ursa.ypp"

    char* sValue;               /* integer value written as string */
    char  bValue;               /* boolean value */
    char* sName;                /* symbol table index */
    nodeType *nPtr;             /* node pointer */

#line 125 "ursa.tab.hpp"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_URSA_TAB_HPP_INCLUDED  */
