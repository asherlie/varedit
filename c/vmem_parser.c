#include "vmem_parser.h"
#include <string.h>
#include <stdlib.h>

// TODO: change this to return int and use the macros maybe add a function to 
const char* which_rgn(struct mem_rgn rgn, void* addr){
      char* addr_c = (char*)addr;
      if(addr_c >= (char*)rgn.stack_start_addr && addr_c <= (char*)rgn.stack_end_addr)return "stack";
      if(addr_c >= (char*)rgn.heap_start_addr && addr_c <= (char*)rgn.heap_end_addr)return "heap";
      for(int i = 0; i < rgn.n_remaining; ++i){
            if(addr_c >= (char*)rgn.remaining_addr[i].first && addr_c <= (char*)rgn.remaining_addr[i].second)return "unmarked region";
      }
      return "";
}

const char* get_proc_name(pid_t pid){        
      char path[100];
      strcpy(path, "/proc/");
      char pid_ch[10];
      sprintf(pid_ch, "%d", pid);
      strcat(path, pid_ch);        
      strcat(path, "/cmdline");              
      char* line;
      FILE* fp = fopen(path, "r");
      size_t sz = 0;
      getline(&line, &sz, fp);
      // maybe replace with getdelim()
      // because the original one did and used '\0' 
      return line;
}                 


struct mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional){
      char* tmp;
      char map_path[100];
      strcpy(map_path, "/proc/");
      char pid_ch[10];
      sprintf(pid_ch, "%d", pid);
      strcat(map_path, pid_ch);        
      strcat(map_path, "/maps");              
      FILE* fp = fopen(map_path, "r");
      bool found_desc;
      unsigned int i;
      struct mem_rgn vmem;
      vmem.p_name = get_proc_name(pid);
      vmem.stack_start_addr = NULL;
      vmem.stack_end_addr = NULL;
      vmem.heap_start_addr = NULL;
      vmem.heap_end_addr = NULL;
      vmem.remaining_addr = (struct m_addr_pair*)malloc(sizeof(struct m_addr_pair)*100); // 100 should be enough
      int rem_alloc_sz = 100;
      vmem.n_remaining = 0;
      void* p_end = NULL;
      size_t sz = 0;
      while(getline(&tmp, &sz, fp) != -1){
            char start_add[20]; int sa_p = 0;
            char end_add[20]; int ea_p = 0;
            //desc[0] = '\0';
            //memset(desc, 0, sizeof(desc));
            found_desc = true;
            i = 0;
            while(tmp[i] != '-'){
                  start_add[sa_p++] = tmp[i];
                  ++i;
            }
            ++i;
            unsigned long* l_start_add = (unsigned long*)strtoul(start_add, 0, 16);
            while(tmp[i] != ' '){
                  end_add[ea_p++] = tmp[i];
                  ++i;
            }
            unsigned long* l_end_add = (unsigned long*)strtoul(end_add, 0, 16);
            if(i == strlen(tmp)){
                  found_desc = false;
                  break;
            }
            // TODO: fix criteria for unmarked additional mem rgns
            while(tmp[i-1] != '/' && tmp[i-1] != '[' && i < strlen(tmp)){
                  if(tmp[i] == '/' || i >= strlen(tmp)-1){
                        if(strstr(tmp, vmem.p_name) != NULL || (unmarked_additional && p_end != l_start_add && i >= strlen(tmp)-1)){
                              struct m_addr_pair tmp_pair;
                              tmp_pair.first = (void*)l_start_add;
                              tmp_pair.second = (void*)l_end_add;
                              //                 n_additional space-1
                              if(vmem.n_remaining == rem_alloc_sz-1){
                                    ++rem_alloc_sz;
                                    struct m_addr_pair* tmp_realloc = (struct m_addr_pair*)malloc(sizeof(struct m_addr_pair)*rem_alloc_sz);
                                    memcpy(tmp_realloc, vmem.remaining_addr, sizeof(struct m_addr_pair)*rem_alloc_sz);
                                    free(vmem.remaining_addr);
                                    vmem.remaining_addr = tmp_realloc;
                              }
                              vmem.remaining_addr[vmem.n_remaining++] = tmp_pair;
                        }
                        found_desc = false;
                  }
                  ++i;
            }
            p_end = l_end_add;
            --i;
            if(found_desc){
                  char desc[20]; int de_p = 0;
                  while(tmp[i-1] != ']'){
                        desc[de_p++] = tmp[i];
                        ++i;
                  }
                  if(strcmp(desc, "[heap]") == 0){
                        vmem.heap_start_addr = (void*)l_start_add;
                        vmem.heap_end_addr = (void*)l_end_add;
                  }
                  if(strcmp(desc, "[stack]") == 0){
                        vmem.stack_start_addr = (void*)l_start_add;
                        vmem.stack_end_addr = (void*)l_end_add;
                  }
            }
      }
      fclose(fp);
      return vmem;
}
