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

int em5_buf_mmap(struct em5_buf *buf, struct vm_area_struct *vma)
/* Map the buffer to userspace */
{
	int i = 0;
	unsigned long uaddr = vma->vm_start;
	unsigned long usize = vma->vm_end - vma->vm_start;
	unsigned long bufsz = 0;
	
	if (!buf) {
		pr_err("No memory to map!\n");
		return -EINVAL;
	}
	
	bufsz = buf->size;
	
	/* do not allow larger mapping then the number of pages allocated */
	if (usize > bufsz) {
		PERROR("You are trying to map larger area.");
		return -EIO;
	}
	
	/* make it foolproof */
	if ( usize != bufsz ){
		PERROR("You can't mmap a part of a buffer.");
		return -EINVAL;
	}
	
	do {
		int ret;
		ret = vm_insert_page(vma, uaddr, buf->pages[i++]);
		if (ret) {
			pr_err("Remapping memory, error: %d\n", ret);
			return ret;
		}
		uaddr += PAGE_SIZE;
		usize -= PAGE_SIZE;
	} while (usize > 0);
	
	
	/* Note: remap_vmalloc_range sets VM_RESERVED flag in vma,
	   so pages does not migrate in memory after that.
	*/
	//~ ret = remap_vmalloc_range( vma, buf->vaddr, 0 /* page offset */ );
	//~ if (ret) {
		//~ PERROR("Failed to remap vmalloc range, addr: %x.", (uint)buf->vaddr);
	//~ }
	
	vma->vm_flags |= VM_RESERVED;
	
	return 0;
}


int em5_buf_init(struct em5_buf *buf, size_t size)
/** allocate readout buffer */
{	
	int i;
	unsigned int sz;
	buf->vaddr = NULL;
	buf->num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT; //round a size to PAGE_SIZE
	
	
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
	
	/*fill buffer */
	for (i = 0; i < (buf->num_pages); i++) {
		*(int*)(buf->vaddr + PAGE_SIZE * i) = i;
	}
	
	
	buf->size = buf->num_pages * PAGE_SIZE;
	buf->count = 0;
	
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
