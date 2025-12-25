void pmm_init(void);
void* alloc_page(void);
void free_page(void *page);
void* alloc_pages(int n);
void page_incref(void *page);
int page_decref(void *page);
int page_refcount(void *page);

