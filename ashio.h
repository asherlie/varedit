#define ASHIO_VER "1.7.1"

#define LOW_MEM 0

#include <pthread.h>

struct tabcom_entry{
      void* data_douplep;
      int data_blk_sz, data_offset, optlen;
};

struct tabcom{
      struct tabcom_entry* tbce;
      int n, cap, n_flattened;
};

struct gr_subroutine_arg{
      char* char_recvd;
      char** str_recvd;

      /* set by user */
      /* most of the time pthread_arg will be a struct with a member that is struct gr_subroutine_arg* */
      void* pthread_arg;
      
      /* these are meant for internal use */
      _Bool* join_pth;
      pthread_t prev_th;
};

void init_gsa(struct gr_subroutine_arg* gsa);
void free_gsa(struct gr_subroutine_arg* gsa);

/* tabcom operations */

struct tabcom* init_tabcom(struct tabcom* tbc);
void free_tabcom(struct tabcom* tbc);
int insert_tabcom(struct tabcom* tbc, void* data_douplep, int data_blk_sz, int data_offset, int optlen);
struct tabcom_entry pop_tabcom(struct tabcom* tbc);

/* term behavior */

void raw_mode();
void reset_term();

/* reading from stdin */

char* getline_raw(int* bytes_read, _Bool* tab, int* ignore);
char* getline_raw_sub(int* bytes_read, _Bool* tab, int* ignore, void *(*routine)(void *), struct gr_subroutine_arg* param);

char* tab_complete(struct tabcom* tbc, char iter_opts[2], int* bytes_read, _Bool* free_s);
