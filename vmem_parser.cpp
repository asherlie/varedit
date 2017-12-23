#include "vmem_parser.h"
#include <stdlib.h>
#include <fstream>

std::string get_proc_name(pid_t pid){
      std::string pname;
      std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
      std::ifstream ifs(path);
      // /proc/pid/cmdline is delimeted by a NUL character
      std::getline(ifs, pname, '\0');
      ifs.close();
      return pname;
}

mem_rgn get_vmem_locations(pid_t pid, bool unmarked_additional){
      std::string tmp;
      std::string map_path = "/proc/" + std::to_string(pid) + "/maps";
      std::ifstream ifs(map_path);
      std::string start_add, end_add, desc;
      bool found_desc;
      unsigned int i;
      mem_rgn vmem;
      vmem.p_name = get_proc_name(pid);
      vmem.stack_start_addr = nullptr;
      vmem.stack_end_addr = nullptr;
      vmem.heap_start_addr = nullptr;
      vmem.heap_end_addr = nullptr;
      vmem.remaining_addr = new std::pair<void*, void*>[100];// should be enough
      int rem_alloc_sz = 100;
      vmem.n_remaining = 0;
      void* p_end = nullptr;
      while(std::getline(ifs, tmp)){
            start_add = "";
            end_add = "";
            desc = "";
            found_desc = true;
            i = 0;
            while(tmp[i] != '-'){
                  start_add += tmp[i];
                  ++i;
            }
            ++i;
            const char* start_add_cstr = start_add.c_str();
            unsigned long* l_start_add = (unsigned long*)strtoul(start_add_cstr, 0, 16);
            while(tmp[i] != ' '){
                  end_add += tmp[i];
                  ++i;
            }
            const char* end_add_cstr = end_add.c_str();
            unsigned long* l_end_add = (unsigned long*)strtoul(end_add_cstr, 0, 16);
            if(i == tmp.size()){
                  found_desc = false;
                  break;
            }
            while(tmp[i-1] != '/' && tmp[i-1] != '[' && i < tmp.size()){
                  if(tmp[i] == '/' || i >= tmp.size()-1){
                        if(tmp.find(vmem.p_name) != std::string::npos || (unmarked_additional && p_end != l_start_add && i >= tmp.size()-1)){
                              std::pair<void*, void*> tmp_pair((void*)l_start_add, (void*)l_end_add);
                              //                 n_additional space-1
                              if(vmem.n_remaining == rem_alloc_sz-1){
                                    ++rem_alloc_sz;
                                    std::pair<void*, void*>* tmp_realloc = new std::pair<void*, void*>[rem_alloc_sz];
                                    std::copy(vmem.remaining_addr, vmem.remaining_addr+vmem.n_remaining-1, tmp_realloc);
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
                  while(tmp[i-1] != ']'){
                        desc += tmp[i];
                        ++i;
                  }
                  if(desc == "[heap]"){
                        vmem.heap_start_addr = (void*)l_start_add;
                        vmem.heap_end_addr = (void*)l_end_add;
                  }
                  if(desc == "[stack]"){
                        vmem.stack_start_addr = (void*)l_start_add;
                        vmem.stack_end_addr = (void*)l_end_add;
                  }
            }
      }
      ifs.close();
      return vmem;
}
