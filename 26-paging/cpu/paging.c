#include "paging.h"
#include "../drivers/screen.h"
// A bitset of frames - used or free.
u32 *frames;
u32 nframes;
page_directory_t *kernel_directory;
page_directory_t *current_directory;

// Defined in kheap.c
extern u32 free_mem_addr;
u32 page_directory[1024] __attribute__((aligned(4096)));
u32 first_page_table[1024] __attribute__((aligned(4096)));

// Macros used in the bitset algorithms.
#define INDEX_FROM_BIT(a) (a / (8 * 4))
#define OFFSET_FROM_BIT(a) (a % (8 * 4))

void page_fault(registers_t regs)
{
    // A page fault has occurred.
    // The faulting address is stored in the CR2 register.
    // u32 faulting_address;
    // asm volatile("mov %%cr2, %0"
    //              : "=r"(faulting_address));

    // The error code gives us details of what happened.
    int present = !(regs.err_code & 0x1); // Page not present
    int rw = regs.err_code & 0x2;         // Write operation?
    int us = regs.err_code & 0x4;         // Processor was in user-mode?
    int reserved = regs.err_code & 0x8;   // Overwritten CPU-reserved bits of page entry?
    // int id = regs.err_code & 0x10;        // Caused by an instruction fetch?

    // Output an error message.
    kprint("Page fault! ( ");
    if (present)
    {
        kprint("present ");
    }
    if (rw)
    {
        kprint("read-only ");
    }
    if (us)
    {
        kprint("user-mode ");
    }
    if (reserved)
    {
        kprint("reserved ");
    }
    kprint(") at 0x");
    // kprint(faulting_address);
    kprint("\n");
}

// Static function to set a bit in the frames bitset
static void set_frame(u32 frame_addr)
{
    u32 frame = frame_addr / 0x1000;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    frames[idx] |= (0x1 << off);
}

// Static function to clear a bit in the frames bitset
static void clear_frame(u32 frame_addr)
{
    u32 frame = frame_addr / 0x1000;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    frames[idx] &= ~(0x1 << off);
}

// Static function to test if a bit is set.
static u32 test_frame(u32 frame_addr)
{
    u32 frame = frame_addr / 0x1000;
    u32 idx = INDEX_FROM_BIT(frame);
    u32 off = OFFSET_FROM_BIT(frame);
    return (frames[idx] & (0x1 << off));
}

// Static function to find the first free frame.
static u32 first_frame()
{
    u32 i, j;
    for (i = 0; i < INDEX_FROM_BIT(nframes); i++)
    {
        if (frames[i] != 0xFFFFFFFF) // nothing free, exit early.
        {
            // at least one bit is free here.
            for (j = 0; j < 32; j++)
            {
                u32 toTest = 0x1 << j;
                if (!(frames[i] & toTest))
                {
                    return i * 4 * 8 + j;
                }
            }
        }
    }
}

void alloc_frame(page_t *page, int is_kernel, int is_writeable)
{
    if (page->frame != 0)
    {
        return; // Frame was already allocated, return straight away.
    }
    else
    {
        u32 idx = first_frame(); // idx is now the index of the first free frame.
        if (idx == (u32)-1)
        {

            // PANIC is just a macro that prints a message to the screen then hits an infinite loop.
        }
        set_frame(idx * 0x1000);           // this frame is now ours!
        page->present = 1;                 // Mark it as present.
        page->rw = (is_writeable) ? 1 : 0; // Should the page be writeable?
        page->user = (is_kernel) ? 0 : 1;  // Should the page be user-mode?
        page->frame = idx;
    }
}

// Function to deallocate a frame.
void free_frame(page_t *page)
{
    u32 frame;
    if (!(frame = page->frame))
    {
        return; // The given page didn't actually have an allocated frame!
    }
    else
    {
        clear_frame(frame); // Frame is now free again.
        page->frame = 0x0;  // Page now doesn't have a frame.
    }
}

void initialise_paging()
{
    // The size of physical memory. For the moment we
    // assume it is 16MB big.
    // u32 mem_end_page = 0x1000000;

    // nframes = mem_end_page / 0x1000;
    // frames = (u32 *)kmalloc(INDEX_FROM_BIT(nframes), 0, 0);
    // memory_set(frames, 0, INDEX_FROM_BIT(nframes));

    // // // Let's make a page directory.
    // kernel_directory = (page_directory_t *)kmalloc(sizeof(page_directory_t), 1, 0);
    // memory_set(kernel_directory, 0, sizeof(page_directory_t));
    // current_directory = kernel_directory;

    // // We need to identity map (phys addr = virt addr) from
    // // 0x0 to the end of used memory, so we can access this
    // // transparently, as if paging wasn't enabled.
    // // NOTE that we use a while loop here deliberately.
    // // inside the loop body we actually change placement_address
    // // by calling kmalloc(). A while loop causes this to be
    // // computed on-the-fly rather than once at the start.
    // int i = 0;
    // while (i < free_mem_addr)
    // {
    //     // Kernel code is readable but not writeable from userspace.
    //     alloc_frame(get_page(i, 1, kernel_directory), 0, 0);
    //     i += 0x1000;
    // }

    for (int i = 0; i < 1024; i++)
    {
        // This sets the following flags to the pages:
        //   Supervisor: Only kernel-mode can access them
        //   Write Enabled: It can be both read from and written to
        //   Not Present: The page table is not present
        page_directory[i] = 0x00000002;
    }

    // page_directory[1000] = 0x00000002;

    // for (int i = 500; i < 1000; i++)
    // {
    //     // This sets the following flags to the pages:
    //     //   Supervisor: Only kernel-mode can access them
    //     //   Write Enabled: It can be both read from and written to
    //     //   Not Present: The page table is not present
    //     page_directory[i] = 0x00000002;
    // }

    //we will fill all 1024 entries in the table, mapping 4 megabytes
    for (unsigned int i = 0; i < 1024; i++)
    {
        first_page_table[i] = (i * 0x1000) | 3; // attributes: supervisor level, read/write, present.
    }

    page_directory[0] = ((unsigned int)first_page_table) | 3;

    // Before we enable paging, we must register our page fault handler.
    register_interrupt_handler(14, page_fault);

    // Now, enable paging!
    __asm__ __volatile__("mov %0, %%cr3" ::"r"(&page_directory));
    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0"
                         : "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging!
    __asm__ __volatile__("mov %0, %%cr0" ::"r"(cr0));

    // switch_page_directory(kernel_directory);
}

void switch_page_directory(page_directory_t *dir)
{
    current_directory = dir;
    __asm__ __volatile__("mov %0, %%cr3" ::"r"(&dir->tablesPhysical));
    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0"
                         : "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging!
    __asm__ __volatile__("mov %0, %%cr0" ::"r"(cr0));
}

page_t *get_page(u32 address, int make, page_directory_t *dir)
{
    // Turn the address into an index.
    address /= 0x1000;
    // Find the page table containing this address.
    u32 table_idx = address / 1024;
    if (dir->tables[table_idx]) // If this table is already assigned
    {
        return &dir->tables[table_idx]->pages[address % 1024];
    }
    else if (make)
    {
        u32 tmp;
        dir->tables[table_idx] = (page_table_t *)kmalloc(sizeof(page_table_t), 1, &tmp);
        memory_set(dir->tables[table_idx], 0, 0x1000);
        dir->tablesPhysical[table_idx] = tmp | 0x7; // PRESENT, RW, US.
        return &dir->tables[table_idx]->pages[address % 1024];
    }
    else
    {
        return 0;
    }
}
