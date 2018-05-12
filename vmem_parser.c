#include "vmem_parser.h"
#include <string.h>

void free_mem_rgn(struct mem_rgn* mr){
      free(mr->p_name);
      if(mr->n_remaining > 0)free(mr->remaining_addr);
}

const char* which_rgn(struct mem_rgn rgn, void* addr, int* res){
      char* addr_c = (char*)addr;
      if(addr_c >= (char*)rgn.stack_start_addr && addr_c <= (char*)rgn.stack_end_addr){
            if(res)*res = STACK;
            return "stack";
      }
      if(addr_c >= (char*)rgn.heap_start_addr && addr_c <= (char*)rgn.heap_end_addr){
            if(res)*res = HEAP;
            return "heap";
      }
      for(int i = 0; i < rgn.n_remaining; ++i){
            if(addr_c >= (char*)rgn.remaining_addr[i].start && addr_c <= (char*)rgn.remaining_addr[i].end){
                  if(res)*res = 2 + i;
                  return "unmarked region";
            }
      }
      return "";
}

char* get_proc_name(pid_t pid){        
      char path[100];
      strcpy(path, "/proc/");
      sprintf(path, "/proc/%d/cmdline", pid);
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
      sprintf(map_path, "/proc/%d/maps", pid);
      FILE* fp = fopen(map_path, "r");
      struct mem_rgn vmem;
      vmem.p_name = get_proc_name(pid);
      vmem.stack_start_addr = NULL;
      vmem.stack_end_addr = NULL;
      vmem.heap_start_addr = NULL;
      vmem.heap_end_addr = NULL;
      vmem.n_remaining = 0;
      if(fp == NULL)return vmem;
      int rem_alloc_sz = 0;
      void* p_end = NULL;
      size_t sz = 0;
      bool first_unmarked = true;
      while(getline(&tmp, &sz, fp) != -1){
            char* start_add = tmp;
            char* end_add = strchr(tmp, '-');
            char* space = strchr(end_add, ' ');
            *(space++) = '\0';
            *(end_add++) = '\0';
            unsigned long* l_start_add = (unsigned long*)strtoul(start_add, NULL, 16);
            unsigned long* l_end_add = (unsigned long*)strtoul(end_add, NULL, 16);
            char* sl = strchr(space, '/');
            if(sl){
                  if(p_end != l_start_add && (strstr(sl, vmem.p_name) || unmarked_additional)){
                        struct m_addr_pair tmp_pair;
                        tmp_pair.start = (void*)l_start_add;
                        tmp_pair.end = (void*)l_end_add;
                        if(vmem.n_remaining == rem_alloc_sz){
                              ++rem_alloc_sz;
                              if(first_unmarked){
                                    first_unmarked = false;
                                    vmem.remaining_addr = malloc(sizeof(struct m_addr_pair)*rem_alloc_sz);
                              }
                              else {
                                    struct m_addr_pair* tmp_realloc = malloc(sizeof(struct m_addr_pair)*rem_alloc_sz);
                                    memcpy(tmp_realloc, vmem.remaining_addr, sizeof(struct m_addr_pair)*(rem_alloc_sz-1));
                                    free(vmem.remaining_addr);
                                    vmem.remaining_addr = tmp_realloc;
                              }
                        }
                        vmem.remaining_addr[vmem.n_remaining++] = tmp_pair;
                  }
            }
            char* desc = strchr(space, '[');
            if(desc){
                  *strchr(desc, ']') = '\0';
                  ++desc;
                  if(strcmp(desc, "heap") == 0){
                        vmem.heap_start_addr = (void*)l_start_add;
                        vmem.heap_end_addr = (void*)l_end_add;
                  }
                  if(strcmp(desc, "stack") == 0){
                        vmem.stack_start_addr = (void*)l_start_add;
                        vmem.stack_end_addr = (void*)l_end_add;
                  }
            }
            p_end = l_end_add;
      }
      // free memory allocated by getline
      free(tmp);
      fclose(fp);
      return vmem;
}
