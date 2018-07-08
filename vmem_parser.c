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
      char path[30];
      sprintf(path, "/proc/%i/cmdline", pid);
      char* line = NULL;
      FILE* fp = fopen(path, "r");
      if(!fp)return line;
      size_t sz = 0;
      getline(&line, &sz, fp);
      fclose(fp);
      return line;
}                 


struct mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional){
      char* tmp = NULL;
      char map_path[30];
      sprintf(map_path, "/proc/%i/maps", pid);
      struct mem_rgn vmem;
      vmem.pid = pid;
      vmem.p_name = get_proc_name(pid);
      vmem.heap.end = vmem.heap.start = vmem.stack.end = vmem.stack.start = NULL;
      vmem.n_remaining = 0;
      FILE* fp = fopen(map_path, "r");
      if(!fp)return vmem;
      int rem_alloc_sz = 0;
      void* p_end = NULL;
      size_t sz = 0;
      while(getline(&tmp, &sz, fp) != EOF){
            char* start_add = tmp;
            char* end_add = strchr(tmp, '-');
            char* space = strchr(end_add, ' ');
            *(space++) = '\0';
            *(end_add++) = '\0';
            void* l_start_add = (void*)strtoul(start_add, NULL, 16);
            void* l_end_add = (void*)strtoul(end_add, NULL, 16);
            char* sl;
            // TODO: make the criteria for unmarked_additional more strict/correct, too many regions are being returned
            // TODO: look into the p_end != l_start_add precaution, it is only correct to use as a criterion in some circumstances
            // in others, it will ignore important values, for example, variables in the python interpreter
            // the commented line below is much less strict for unmarked_additional
            /*if(unmarked_additional || (p_end != l_start_add && ((sl = strchr(space, '/')) && strstr(sl, vmem.p_name)))){*/
            if(p_end != l_start_add && (unmarked_additional || (((sl = strchr(space, '/')) && strstr(sl, vmem.p_name))))){
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
