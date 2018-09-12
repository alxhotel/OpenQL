#define OPENQL_MAJOR_VERSION 0

#define OPENQL_MINOR_VERSION 5

#define OPENQL_PATCH_VERSION 0

// Make it easier to check for QISA version dependencies.
// This assumes the PATCH and MINOR version will not exceed 99
#define OPENQL_FULL_VERSION (OPENQL_MAJOR_VERSION * 10000 + OPENQL_MINOR_VERSION * 100 + OPENQL_PATCH_VERSION)

#define OPENQL_VERSION_STRING_S1(arg) #arg
#define OPENQL_VERSION_STRING_S(arg) OPENQL_VERSION_STRING_S1(arg)

#define OPENQL_VERSION_STRING (OPENQL_VERSION_STRING_S(OPENQL_MAJOR_VERSION) "." \
                             OPENQL_VERSION_STRING_S(OPENQL_MINOR_VERSION) "." \
OPENQL_VERSION_STRING_S(OPENQL_PATCH_VERSION))
