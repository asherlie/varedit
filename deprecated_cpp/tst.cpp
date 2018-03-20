#include <iostream>
#include <unistd.h>

int main(){
      int n = 234599;
      int n_o = n;
      std::cout << "pid: " << getpid() << std::endl;
      std::cout << "integer has value " << n << std::endl;
      while(1){
            if(n != n_o){
                  std::cout << "integer has been changed to " << n << std::endl;
                  n_o = n;
            }
      }
}
