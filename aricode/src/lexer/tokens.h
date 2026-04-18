#ifndef ARICODE_TOKENS_H
#define ARICODE_TOKENS_H

typedef enum {
    /* Special */
    TOKEN_EOF = 0,
    TOKEN_ILLEGAL,

    /* Literals */
    TOKEN_INTEGER,       /* 42, 0xFF, 0b1010 */
    TOKEN_FLOAT,         /* 3.14 */
    TOKEN_STRING,        /* "hello" */
    TOKEN_IDENTIFIER,    /* variable/function names */

    /* Keywords */
    TOKEN_FN,
    TOKEN_LET,
    TOKEN_CONST,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_RETURN,
    TOKEN_MATCH,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_ERROR,
    TOKEN_LOG,
    TOKEN_IMPORT,
    TOKEN_EXPORT,
    TOKEN_IN,
    TOKEN_STRUCT,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_SOME,
    TOKEN_NONE,
    TOKEN_OPTION,

    /* Type keywords */
    TOKEN_TYPE_I8,
    TOKEN_TYPE_I16,
    TOKEN_TYPE_I32,
    TOKEN_TYPE_I64,
    TOKEN_TYPE_U8,
    TOKEN_TYPE_U16,
    TOKEN_TYPE_U32,
    TOKEN_TYPE_U64,
    TOKEN_TYPE_F32,
    TOKEN_TYPE_F64,
    TOKEN_TYPE_DEC,      /* dec (arbitrary precision decimal) */
    TOKEN_TYPE_STR,
    TOKEN_TYPE_BOOL,
    TOKEN_TYPE_ARR,
    TOKEN_TYPE_MAP,

    /* Error levels */
    TOKEN_LEVEL_SILENT,       /* Level.SILENT */
    TOKEN_LEVEL_LOGIC,        /* Level.LOGIC */
    TOKEN_LEVEL_WARNING,      /* Level.WARNING */
    TOKEN_LEVEL_SYSTEM,       /* Level.SYSTEM */
    TOKEN_LEVEL_CATASTROPHIC, /* Level.CATASTROPHIC */

    /* Operators */
    TOKEN_PLUS,          /* + */
    TOKEN_MINUS,         /* - */
    TOKEN_STAR,          /* * */
    TOKEN_SLASH,         /* / */
    TOKEN_PERCENT,       /* % */
    TOKEN_ASSIGN,        /* = */
    TOKEN_PLUS_ASSIGN,   /* += */
    TOKEN_MINUS_ASSIGN,  /* -= */
    TOKEN_STAR_ASSIGN,   /* *= */
    TOKEN_SLASH_ASSIGN,  /* /= */
    TOKEN_EQ,            /* == */
    TOKEN_NEQ,           /* != */
    TOKEN_LT,            /* < */
    TOKEN_GT,            /* > */
    TOKEN_LTE,           /* <= */
    TOKEN_GTE,           /* >= */
    TOKEN_AND,           /* && */
    TOKEN_OR,            /* || */
    TOKEN_NOT,           /* ! */
    TOKEN_BIT_AND,       /* & */
    TOKEN_BIT_OR,        /* | */
    TOKEN_BIT_XOR,       /* ^ */
    TOKEN_BIT_NOT,       /* ~ */
    TOKEN_SHL,           /* << */
    TOKEN_SHR,           /* >> */
    TOKEN_QUESTION,      /* ? */

    /* Delimiters */
    TOKEN_LPAREN,        /* ( */
    TOKEN_RPAREN,        /* ) */
    TOKEN_LBRACE,        /* { */
    TOKEN_RBRACE,        /* } */
    TOKEN_LBRACKET,      /* [ */
    TOKEN_RBRACKET,      /* ] */
    TOKEN_SEMICOLON,     /* ; */
    TOKEN_COLON,         /* : */
    TOKEN_COLONCOLON,    /* :: */
    TOKEN_COMMA,         /* , */
    TOKEN_DOT,           /* . */
    TOKEN_ARROW,         /* -> */
    TOKEN_FAT_ARROW,     /* => */

    TOKEN_ENUM,          /* enum keyword (placed at end to avoid renumbering) */
    TOKEN_COUNT          /* total number of token types */
} TokenType;

/* Human-readable names for debugging/error messages */
static const char *token_type_names[] = {
    [TOKEN_EOF]               = "EOF",
    [TOKEN_ILLEGAL]           = "ILLEGAL",
    [TOKEN_INTEGER]           = "INTEGER",
    [TOKEN_FLOAT]             = "FLOAT",
    [TOKEN_STRING]            = "STRING",
    [TOKEN_IDENTIFIER]        = "IDENTIFIER",
    [TOKEN_FN]                = "fn",
    [TOKEN_LET]               = "let",
    [TOKEN_CONST]             = "const",
    [TOKEN_IF]                = "if",
    [TOKEN_ELSE]              = "else",
    [TOKEN_FOR]               = "for",
    [TOKEN_WHILE]             = "while",
    [TOKEN_RETURN]            = "return",
    [TOKEN_MATCH]             = "match",
    [TOKEN_TRY]               = "try",
    [TOKEN_CATCH]             = "catch",
    [TOKEN_ERROR]             = "error",
    [TOKEN_LOG]               = "log",
    [TOKEN_IMPORT]            = "import",
    [TOKEN_EXPORT]            = "export",
    [TOKEN_IN]                = "in",
    [TOKEN_STRUCT]            = "struct",
    [TOKEN_ENUM]              = "enum",
    [TOKEN_COLONCOLON]        = "::",
    [TOKEN_BREAK]             = "break",
    [TOKEN_CONTINUE]          = "continue",
    [TOKEN_TRUE]              = "true",
    [TOKEN_FALSE]             = "false",
    [TOKEN_SOME]              = "Some",
    [TOKEN_NONE]              = "None",
    [TOKEN_OPTION]            = "Option",
    [TOKEN_TYPE_I8]           = "i8",
    [TOKEN_TYPE_I16]          = "i16",
    [TOKEN_TYPE_I32]          = "i32",
    [TOKEN_TYPE_I64]          = "i64",
    [TOKEN_TYPE_U8]           = "u8",
    [TOKEN_TYPE_U16]          = "u16",
    [TOKEN_TYPE_U32]          = "u32",
    [TOKEN_TYPE_U64]          = "u64",
    [TOKEN_TYPE_F32]          = "f32",
    [TOKEN_TYPE_F64]          = "f64",
    [TOKEN_TYPE_DEC]          = "dec",
    [TOKEN_TYPE_STR]          = "str",
    [TOKEN_TYPE_BOOL]         = "bool",
    [TOKEN_TYPE_ARR]          = "arr",
    [TOKEN_TYPE_MAP]          = "map",
    [TOKEN_LEVEL_SILENT]      = "Level.SILENT",
    [TOKEN_LEVEL_LOGIC]       = "Level.LOGIC",
    [TOKEN_LEVEL_WARNING]     = "Level.WARNING",
    [TOKEN_LEVEL_SYSTEM]      = "Level.SYSTEM",
    [TOKEN_LEVEL_CATASTROPHIC]= "Level.CATASTROPHIC",
    [TOKEN_PLUS]              = "+",
    [TOKEN_MINUS]             = "-",
    [TOKEN_STAR]              = "*",
    [TOKEN_SLASH]             = "/",
    [TOKEN_PERCENT]           = "%",
    [TOKEN_ASSIGN]            = "=",
    [TOKEN_EQ]                = "==",
    [TOKEN_NEQ]               = "!=",
    [TOKEN_LT]                = "<",
    [TOKEN_GT]                = ">",
    [TOKEN_LTE]               = "<=",
    [TOKEN_GTE]               = ">=",
    [TOKEN_AND]               = "&&",
    [TOKEN_OR]                = "||",
    [TOKEN_NOT]               = "!",
    [TOKEN_BIT_AND]           = "&",
    [TOKEN_BIT_OR]            = "|",
    [TOKEN_BIT_XOR]           = "^",
    [TOKEN_BIT_NOT]           = "~",
    [TOKEN_SHL]               = "<<",
    [TOKEN_SHR]               = ">>",
    [TOKEN_LPAREN]            = "(",
    [TOKEN_RPAREN]            = ")",
    [TOKEN_LBRACE]            = "{",
    [TOKEN_RBRACE]            = "}",
    [TOKEN_LBRACKET]          = "[",
    [TOKEN_RBRACKET]          = "]",
    [TOKEN_SEMICOLON]         = ";",
    [TOKEN_COLON]             = ":",
    [TOKEN_COMMA]             = ",",
    [TOKEN_DOT]               = ".",
    [TOKEN_ARROW]             = "->",
    [TOKEN_FAT_ARROW]         = "=>",
};

#endif /* ARICODE_TOKENS_H */
