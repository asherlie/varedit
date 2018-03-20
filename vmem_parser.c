#include "vmem_parser.h"
#include <string.h>
#include <stdlib.h>

void free_mem_rgn(struct mem_rgn* mr){
      free(mr->p_name);
      if(mr->n_remaining > 0)free(mr->remaining_addr);
}

bool is_substr(const char* substr, const char* str){
      int len = strlen(substr);
      char buf[len+1];
      int i = 0;
      while(str[i+len-1] != '\0'){
            memset(buf, '\0', len);
            memcpy(buf, str+(i++), len);
            buf[len]='\0';
            if(strcmp(buf, substr) == 0)return true;
      }
      return false;
}

const char* which_rgn(struct mem_rgn rgn, void* addr){
      char* addr_c = (char*)addr;
      if(addr_c >= (char*)rgn.stack_start_addr && addr_c <= (char*)rgn.stack_end_addr)return "stack";
      if(addr_c >= (char*)rgn.heap_start_addr && addr_c <= (char*)rgn.heap_end_addr)return "heap";
      for(int i = 0; i < rgn.n_remaining; ++i){
            if(addr_c >= (char*)rgn.remaining_addr[i].start && addr_c <= (char*)rgn.remaining_addr[i].end)return "unmarked region";
      }
      return "";
}

char* get_proc_name(pid_t pid){        
      char path[100];
      strcpy(path, "/proc/");
      sprintf(path, "/proc/%d/cmdline", pid);
      char* line;
      FILE* fp = fopen(path, "r");
      if(fp == NULL)return line;
      size_t sz = 0;
      getline(&line, &sz, fp);
      // maybe replace with getdelim()
      // because the original one did and used '\0' 
      fclose(fp);
      return line;
}                 


struct mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional){
      char* tmp;
      char map_path[100];
      sprintf(map_path, "/proc/%d/maps", pid);
      FILE* fp = fopen(map_path, "r");
      bool found_desc;
      unsigned int i;
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
            char start_add[20]; int sa_p = 0;
            char end_add[20]; int ea_p = 0;
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
                        if(is_substr(vmem.p_name, tmp) || (unmarked_additional && p_end != l_start_add && i >= strlen(tmp)-1)){
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
                                          memcpy(tmp_realloc, vmem.remaining_addr, sizeof(struct m_addr_pair)*rem_alloc_sz);
                                          free(vmem.remaining_addr);
                                          vmem.remaining_addr = tmp_realloc;
                                    }
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
      // free memory allocated by getline
      free(tmp);
      fclose(fp);
      return vmem;
}
