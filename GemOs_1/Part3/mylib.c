#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

// structure of a free chunk : ptr[0] -> size , ptr[1] -> address of next "free" chunk , ptr[2] -> address of previous "free" chunk
// structure of a allocated memory : ptr[0] -> size

unsigned long *head = NULL;
#define FMB  (4 * 1024 * 1024) // bytes

unsigned long max(unsigned long a, unsigned long b)
{
	if(a > b)
	{
		return a;
	}
	else
	{
		return b;
	}
}

unsigned long * Do_Changes(unsigned long *ptr, unsigned long size)
{
	unsigned long rem = ptr[0] - size;
	if(rem <= 24)
	{
		if(ptr[2] == (unsigned long)NULL)
		{
			head = (unsigned long *)head[1];
			head[2] = (unsigned long)NULL;
		}
		else
		{
			unsigned long *p_ptr = (unsigned long *)ptr[2];
			unsigned long *n_ptr = (unsigned long *)ptr[1];
			p_ptr[1] = (unsigned long)n_ptr;
			if(n_ptr != (unsigned long *)NULL)
			{
				n_ptr[2] = (unsigned long)p_ptr;
			}
		}
		return ptr + 1;
	}
	else
	{
		if(ptr[2] == (unsigned long)NULL)
		{
			unsigned long *n_ptr = ptr + size / 8;
			head = n_ptr;
			n_ptr[0] = rem;
			n_ptr[2] = (unsigned long)NULL;
			n_ptr[1] = ptr[1];
		}
		else
		{
			unsigned long *n_ptr = ptr + size / 8;
			unsigned long *p_ptr = (unsigned long *)ptr[2];
			unsigned long *nxt_ptr = (unsigned long *)ptr[1];
			p_ptr[1] = (unsigned long)n_ptr;
			if(nxt_ptr != (unsigned long *)NULL)
			{
				nxt_ptr[2] = (unsigned long)n_ptr;
			}
			n_ptr[0] = rem;
			n_ptr[2] = (unsigned long)p_ptr;
			n_ptr[1] = (unsigned long)nxt_ptr;
		}
		ptr[0] = size;
		return ptr + 1;
	}
}

void *memalloc(unsigned long size)	
{
	if(size == 0)
	{
		return NULL;
	}
        if(head == (unsigned long *)NULL)
	{
		size += (8 - (size % 8)) % 8;
		size = (size + 8 >= 24) ? size : 16;
		unsigned long required_size = ((size + 8 + FMB - 1) / FMB) * FMB;
		unsigned long *ptr = (unsigned long*)mmap(NULL, required_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if(ptr == NULL)
		{
			//perror("Unable to execute\n");
			return NULL;
		}
		unsigned long rem = required_size - size - 8;
		if(rem <= 24)
		{
			ptr[0] = required_size;
			return ptr + 1;
		}
		else
		{
			ptr[0] = size + 8;
			head = ptr + (size + 8) / 8;
			head[0] = rem;
			head[2] = (unsigned long)NULL;
			head[1] = (unsigned long)NULL;
			return ptr + 1;
		}
	}
	else
	{
		size += (8 - (size % 8)) % 8;
		size = (size + 8 >= 24) ? size : 16;
		unsigned long *temp = head;
		unsigned long *p_ptr = (unsigned long *)NULL;
		while(temp != (unsigned long *)NULL && temp[0] < size + 8)
		{
			p_ptr = temp;
			temp = (unsigned long *)temp[1];
		}
		if(temp == (unsigned long *)NULL)
		{
			unsigned long required_size = ((size + 8 + FMB - 1) / FMB) * FMB;
		        unsigned long *ptr = (unsigned long *)mmap(NULL, required_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if(ptr == NULL)
			{
				//perror("Unable to execute\n");
				return NULL;
			}
			unsigned long rem = required_size - size - 8;
			if(rem <= 24)
			{
				ptr[0] = required_size;
			}
			else
			{
				ptr[0] = size + 8;
				unsigned long *n_ptr = ptr + ptr[0] / 8;
				unsigned long *first = head;
				n_ptr[0] = rem;
				n_ptr[1] = (unsigned long)first;
				n_ptr[2] = (unsigned long)NULL;
				first[2] = (unsigned long)n_ptr;
				head = first;
			}
			return ptr + 1;
		}
		else
		{
			return Do_Changes(temp, size + 8);
		}
	
	}
}
int memfree(void *ptr)
{    
	if(ptr == NULL)
	{
		return -1;
	}
	unsigned long *p = (unsigned long *)ptr;
	p--;
	unsigned long tot_sz = p[0];
	unsigned long *right = p + tot_sz / 8;
	unsigned long *temp1 = head, *temp2 = head;
	//finding right free memory
	while(temp1 != (unsigned long *)NULL)
	{
		if(temp1 == right)
		{
			break;
		}
		temp1 = (unsigned long *)temp1[1];
	}
	//finding left free memory
	while(temp2 != (unsigned long *)NULL)
	{
		if(temp2 + temp2[0] / 8 == p)
		{
			break;
		}
		temp2 = (unsigned long *)temp2[1];
	}
	//case 1 :
	if(temp1 == (unsigned long *)NULL && temp2 == (unsigned long *)NULL)
	{
		p[0] = tot_sz;
		unsigned long *first = head;
		p[1] = (unsigned long)first;
		p[2] = (unsigned long)NULL;
		if(first != (unsigned long *)NULL)
		{
			first[2] = (unsigned long)p;
		}
		head = p;
	}
	// case 2 : 
	else if(temp1 != (unsigned long *)NULL && temp2 == (unsigned long *)NULL)
	{
		p[0] = tot_sz + temp1[0];
		if(head != temp1)
		{
			unsigned long *prev = (unsigned long *)temp1[2];
			unsigned long *next = (unsigned long *)temp1[1];
			prev[1] = (unsigned long)next;
			if(next != (unsigned long *)NULL)
			{
				next[2] = (unsigned long)prev;
			}
			p[1] = (unsigned long)head;
			head = p;
			p[2] = (unsigned long)NULL;
		}
		else
		{
			unsigned long *first = (unsigned long *)head;
			if(first != (unsigned long *)NULL)
			{
				first[2] = (unsigned long)p;
			}
			p[1] = (unsigned long)first;
			p[2] = (unsigned long)NULL;
			head = p;
		}

	}
	//case 3 :
	else if (temp1 == (unsigned long *)NULL && temp2 != (unsigned long *)NULL)
	{
		if(head != temp2)
		{
			temp2[0] += tot_sz;
			unsigned long *next = (unsigned long *)temp2[1];
			unsigned long *prev = (unsigned long *)temp2[2];
			if(prev != (unsigned long *)NULL)
			{
				prev[1] = (unsigned long)next;
			}
			if(next != (unsigned long *)NULL)
			{
				next[2] = (unsigned long)prev;
			}
			temp2[2] = (unsigned long)NULL;
			unsigned long *first = head;
			temp2[1] = (unsigned long)first;
			if(first != (unsigned long *)NULL)
			{
				first[2] = (unsigned long)temp2;
			}
			head = temp2;
		}
		else
		{
			temp2[0] += tot_sz;
		}
	}
	//case 4 :
	else
	{

		// Removing temp1 
		if(temp1 != head)
		{
			unsigned long *prev = (unsigned long *)temp1[2];
			unsigned long *next = (unsigned long *)temp1[1];
			prev[1] = (unsigned long)next;
			if(next != (unsigned long *)NULL)
			{
				next[2] = (unsigned long)prev;
			}
		}
		else
		{
			head = (unsigned long *)temp1[1];
		}
		// Removing temp2 
		if(temp2 != head)
		{
			unsigned long *prev = (unsigned long *)temp2[2];
			unsigned long *next = (unsigned long *)temp2[1];
			prev[1] = (unsigned long)next;
			if(next != (unsigned long *)NULL)
			{
				next[2] = (unsigned long)prev; 
			}
		}
		else
		{
			head = (unsigned long *)temp2[1];
		}
		// merging step
		temp2[0] += temp1[0] + tot_sz;
		// adding the merged , at the head of the free list 
		unsigned long *first = (unsigned long *)head;
		head = temp2;
		temp2[1] = (unsigned long)first;
		if(first != (unsigned long *)NULL)
		{
			first[2] = (unsigned long)temp1;
		}
		temp2[2] = (unsigned long)NULL;
	}
	return 0;
}	
