/* Inspired buy video/videobuf2-dma-sg.c */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/mm.h> /*get_user_pages_fast*/

#include "module.h"
#include "buf.h"

extern struct em5_buf buf;

int em5_buf_mmap( struct vm_area_struct *vma)
/* Map the buffer to userspace */
{
	int ret;
	unsigned long usize = vma->vm_end - vma->vm_start;
	unsigned long bufsize = buf.size;
	
	/* do not allow larger mapping then the number of pages allocated */
	if (usize > bufsize) {
		PERROR("You are trying to map larger area.");
		return -EIO;
	}
	
	/* make it foolproof */
	if ( usize != bufsize ){
		PERROR("You can'y mmap a part of whole buffer.");
		return -EINVAL;
	}
	
	/* Note: remap_vmalloc_range sets VM_RESERVED flag in vma,
	   so pages does not migrate in memory after that.
	*/
	ret = remap_vmalloc_range( vma, buf.vaddr, 0 /* page offset */ );
	
	return ret;
}


int em5_buf_init(struct em5_buf *buf, size_t size)
/** allocate readout buffer */
{	
	int i;
	unsigned int sz;
	buf->vaddr = NULL;
	buf->num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	
	
	sz = buf->num_pages * sizeof(struct page *);
	buf->pages = kzalloc(sz, GFP_KERNEL);
	if (!buf->pages) {
		PERROR("failed to allocate memory for page descriptors (%d bytes).", sz);
		return -ENOMEM;
	}
	
	for (i = 0; i < (buf->num_pages); i++) {
		buf->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (NULL == buf->pages[i]) {
			PERROR("failed to allocate memory for %d pages.", 
					buf->num_pages);
			buf->num_pages = i ? i-1: 0;
			return -ENOMEM;
		}
	}
	
	PDEVEL( "%s: Allocated buffer of %d pages.", __func__, buf->num_pages);
	if (!buf->vaddr)
		buf->vaddr = vm_map_ram(buf->pages, buf->num_pages, -1 /*node*/, PAGE_KERNEL);
	
	buf->size = buf->num_pages * PAGE_SIZE;
	
	return 0;	
}

void em5_buf_free(struct em5_buf *buf)
{
	if ( buf->vaddr) {
		vm_unmap_ram(buf->vaddr, buf->num_pages);
		buf->vaddr = NULL;
		buf->size = 0;
	}
	if (buf->num_pages) {
		int i = buf->num_pages;
		while (--i >= 0)
			__free_page(buf->pages[i]);
		buf->num_pages = 0;
	}
	
	if (buf->pages) 
		kfree(buf->pages);

	return;
}
