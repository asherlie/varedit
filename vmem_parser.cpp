#include "vmem_parser.h"
#include <stdlib.h>
#include <fstream>

mem_rgn get_vmem_locations(pid_t pid){
      std::string tmp;
      std::string map_path = "/proc/" + std::to_string(pid) + "/maps";
      std::ifstream ifs(map_path);
      std::string start_add, end_add, desc;
      bool found_desc;
      unsigned int i;
      mem_rgn vmem;
      vmem.stack_start_addr = nullptr;
      vmem.stack_end_addr = nullptr;
      vmem.heap_start_addr = nullptr;
      vmem.heap_end_addr = nullptr;
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
                  if(i >= tmp.size()-1){
                        found_desc = false;
                  }
                  if(tmp[i] == '/'){
                        found_desc = false;
                  }
                  ++i;
            }
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
