#include "vmem_parser.h"
#include <string.h>

void free_mem_rgn(struct mem_rgn* mr){
      free(mr->p_name);
      if(mr->n_remaining > 0)free(mr->remaining_addr);
}

/*
 * if which_rgn's int* res param is not NULL, it is set 
 * to one of three possible values:
 * STACK, HEAP, or other
 * if (res > HEAP) or (res >= 2), the value of res refers to
 * additional region[res-2]
 */
const char* which_rgn(struct mem_rgn rgn, void* addr, int* res){
      if(addr >= rgn.stack.start && addr <= rgn.stack.end){
            if(res)*res = STACK;
            return "stack";
      }
      if(addr >= rgn.heap.start && addr <= rgn.heap.end){
            if(res)*res = HEAP;
            return "heap";
      }
      for(int i = 0; i < rgn.n_remaining; ++i){
            if(addr >= rgn.remaining_addr[i].start && addr <= rgn.remaining_addr[i].end){
                  if(res)*res = 2 + i;
                  return "unmarked";
            }
      }
      if(res)*res = -1;
      return NULL;
}

char* get_proc_name(pid_t pid){        
      char path[100];
      strcpy(path, "/proc/");
      sprintf(path, "/proc/%i/cmdline", pid);
      char* line = NULL;
      FILE* fp = fopen(path, "r");
      if(fp == NULL)return line;
      size_t sz = 0;
      getline(&line, &sz, fp);
      fclose(fp);
      return line;
}                 


struct mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional){
      char* tmp = NULL;
      char map_path[100];
      sprintf(map_path, "/proc/%i/maps", pid);
      FILE* fp = fopen(map_path, "r");
      struct mem_rgn vmem;
      vmem.p_name = get_proc_name(pid);
      vmem.stack.start = NULL;
      vmem.stack.end = NULL;
      vmem.heap.start = NULL;
      vmem.heap.end = NULL;
      vmem.n_remaining = 0;
      if(fp == NULL)return vmem;
      int rem_alloc_sz = 0;
      void* p_end = NULL;
      size_t sz = 0;
      while(getline(&tmp, &sz, fp) != -1){
            char* start_add = tmp;
            char* end_add = strchr(tmp, '-');
            char* space = strchr(end_add, ' ');
            *(space++) = '\0';
            *(end_add++) = '\0';
            void* l_start_add = (void*)strtoul(start_add, NULL, 16);
            void* l_end_add = (void*)strtoul(end_add, NULL, 16);
            char* sl;
            // TODO: make the criteria for unmarked_additional more strict/correct
            if(unmarked_additional){
                  if(vmem.n_remaining == rem_alloc_sz){
                        ++rem_alloc_sz;
                        if(vmem.n_remaining == 0)vmem.remaining_addr = malloc(sizeof(struct m_addr_pair)*rem_alloc_sz);
                        else{
                              struct m_addr_pair* tmp_realloc = malloc(sizeof(struct m_addr_pair)*rem_alloc_sz);
                              memcpy(tmp_realloc, vmem.remaining_addr, sizeof(struct m_addr_pair)*(rem_alloc_sz-1));
                              free(vmem.remaining_addr);
                              vmem.remaining_addr = tmp_realloc;
                        }
                  }
                  vmem.remaining_addr[vmem.n_remaining].start = l_start_add;
                  vmem.remaining_addr[vmem.n_remaining++].end = l_end_add;
            }
            else if((sl = strchr(space, '/'))){
                  // TODO: look into this precaution - does it make sense
                  if(p_end != l_start_add && (strstr(sl, vmem.p_name))){
                        if(vmem.n_remaining == rem_alloc_sz){
                              ++rem_alloc_sz;
                              if(vmem.n_remaining == 0)vmem.remaining_addr = malloc(sizeof(struct m_addr_pair)*rem_alloc_sz);
                              else{
                                    struct m_addr_pair* tmp_realloc = malloc(sizeof(struct m_addr_pair)*rem_alloc_sz);
                                    memcpy(tmp_realloc, vmem.remaining_addr, sizeof(struct m_addr_pair)*(rem_alloc_sz-1));
                                    free(vmem.remaining_addr);
                                    vmem.remaining_addr = tmp_realloc;
                              }
                        }
                        vmem.remaining_addr[vmem.n_remaining].start = l_start_add;
                        vmem.remaining_addr[vmem.n_remaining++].end = l_end_add;
                  }
            }
            char* desc = strchr(space, '[');
            if(desc){
                  *strchr(desc, ']') = '\0';
                  ++desc;
                  if(strcmp(desc, "heap") == 0){
                        vmem.heap.start = l_start_add;
                        vmem.heap.end = l_end_add;
                  }
                  if(strcmp(desc, "stack") == 0){
                        vmem.stack.start = l_start_add;
                        vmem.stack.end = l_end_add;
                  }
            }
            p_end = l_end_add;
      }
      // free memory allocated by getline
      free(tmp);
      fclose(fp);
      return vmem;
}
