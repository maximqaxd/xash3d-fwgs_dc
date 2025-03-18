

extern table_t lib_filesystem_stdio_exports[];
extern table_t lib_hl_exports[];
extern table_t lib_ref_soft_exports[];
extern table_t lib_ref_gl_exports[];
extern table_t lib_cl_dll_exports[];
extern table_t lib_menu_exports[];

struct {const char *name;void *func;} libs[] = {
{ "filesystem_stdio", &lib_filesystem_stdio_exports },
#if XASH_REF_SOFT_ENABLED
{ "ref_soft", &lib_ref_soft_exports },
#else
{ "ref_gl", &lib_ref_gl_exports },
#endif
{ "server", &lib_hl_exports },
{ "client", &lib_cl_dll_exports },
{ "menu", &lib_menu_exports},
/*
{ "server", &lib_hl_exports },
{ "ref_gl", &lib_ref_gl_exports },

*/
{ 0, 0 }
};
