#pragma once

#include <linux/kernel.h>

// https://stackoverflow.com/questions/9881357/what-is-the-linux-kernel-equivalent-to-the-memset-function
// In sort, just include linux/string.h, because we don't want to confuse include path with c standard
//   library.
#include <linux/string.h>

// Строки должны помещаться в 64 байта с учетом нулевого байта.
//   В utf-8 символы русского языка занимают по 2 байта. Потому
//   Можно будет поместить не более (64 - 1) / 2 (с округлением
//   вниз) = 31 букву русского языка.
struct pb_user_data {
  char first_name[64];
  char last_name[64];
  unsigned int age;
  char telnum[16];
  char email[64];
};
#define PB_PRINT_USER_DATA(printf, ud, prefix, suffix) \
  printf(                                              \
    prefix "(%s, %s, %u, %s, %s)" suffix,              \
    (ud).first_name,                                   \
    (ud).last_name,                                    \
    (ud).age,                                          \
    (ud).telnum,                                       \
    (ud).email                                         \
  )

#define cmp_char_array(lhs, rhs) strncmp(lhs, rhs, sizeof(lhs))
static inline int pb_ud_cmp(const struct pb_user_data* ud1, const struct pb_user_data* ud2) {
  int cmp = 0;

  cmp = cmp_char_array(ud1->first_name, ud2->first_name);
  if (cmp != 0) return cmp;

  cmp = cmp_char_array(ud1->last_name, ud2->last_name);
  if (cmp != 0) return cmp;

  if (ud1->age != ud2->age) {
    if (ud1->age < ud2->age) {
      return -1;
    }

    return 1;
  }

  cmp = cmp_char_array(ud1->telnum, ud2->telnum);
  if (cmp != 0) return cmp;

  cmp = cmp_char_array(ud1->email, ud2->email);
  if (cmp != 0) return cmp;

  return 0;
}
#undef cmp_char_array

static inline void pb_ud_init(struct pb_user_data* ud) {
  memset(ud, 0, sizeof(*ud));
}

static const int PB_OPERATION_ADD               = 1;
static const int PB_OPERATION_FIND_BY_LAST_NAME = 2; // Выдаст все pb_user_data с таким last_name
static const int PB_OPERATION_FIND_BY_ID        = 3;
static const int PB_OPERATION_DELETE            = 4; // Ищет по last_name, а затем среди них ищет то, что надо удалить, удаляет.

#define PB_PHONEBOOK_SIZE 256
#define PB_MSG_BUFFER_LEN sizeof(int) + sizeof(struct pb_user_data)

static const char* const pb_path = "/dev/pbchar";
static const char* const pb_by_lastname_path = "/dev/pb/by-lastname";
// Напоминает /dev/by_label, когда ищется загрузочный раздел у установщика archlinux. 
// Решил загуглить, чтобы была информация. Я просто несколько раз воочию видел.
//   А можно еще почитать.
//   https://yandex.ru/search/?text=dev+by_label&lr=213&clid=1836587
//   Первые несколько ссылок посмотрел:
//     https://zalinux.ru/?p=6662
//     https://wiki.archlinux.org/title/Persistent_block_device_naming
// Оказывается, там by-label, а не by_label. Ок.

// ssize_t pb_add(
