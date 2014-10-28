#ifdef CONFIG_DEBUG_FS
int em5_debugfs_init(void);
void em5_debugfs_free(void);

#else
static inline
int em5_debugfs_init(void)
{
	pr_info( MODULE_NAME " had been built without debugfs support.");
	return 0;
}

static inline
void em5_debugfs_free(void) {};

#endif
