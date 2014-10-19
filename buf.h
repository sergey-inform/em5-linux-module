#ifndef EM5_buf_H
#define EM5_buf_H

/* Readout bufer */
struct em5_buf {
	void * vaddr;
	unsigned long size;
	struct page **pages;
	unsigned int num_pages;
	//~ unsigned long count; /* last written byte */
};

int em5_buf_init(struct em5_buf * buf, size_t);
void em5_buf_free(struct em5_buf * buf);
int em5_buf_mmap(struct vm_area_struct *); 

#endif /*EM5_buf_H*/
