/*
 * Phonebook subsystem for Linux.
 *
 * Copyright (C) 2024       Valeriy Zainullin       (zainullin.vv@phystech.edu)
 *
 * This file is released under the GPL.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>

#include "phonebook.h"

#ifdef CONFIG_MODULES

// From module subsystem internals. $(linux_src)/kernel/module/internal.h
extern struct mutex module_mutex;

// https://stackoverflow.com/questions/23013562/linux-kernel-get-function-address-for-kernel-driver

typedef long (*pb_get_user_t)(const char* surname, unsigned int len, struct pb_user_data* output_data);

typedef long (*pb_add_user_t)(struct pb_user_data* input_data);

// Удаляет всех пользователей с такой фамилией.
typedef long (*pb_del_user_t)(const char* surname, unsigned int len);

// GFP_KERNEL_ACCOUNT.
// https://www.kernel.org/doc/html/next/core-api/memory-allocation.html

static struct module* find_pb_module(void) {
	// Race condition с выгрузкой модуля... Надо какой-то mutex
	//   взять в ядре на загрузку выгрузку модулей. Но это плохо,
	//   один процесс может положить всю работу. Надо запретить
	//   выгружать модуль..
	// https://stackoverflow.com/questions/63867813/stop-unloading-the-linux-kernel-module

	// С try_module_get есть проблема: он принимает struct module*, которого
	//   у нас еще нет. Если посмотреть комментарий к нему в заголовке, то мы
	//   еще должны убедиться, что модуль не выгружается.
	// Вдохновимся: https://stackoverflow.com/questions/10627738/check-if-linux-kernel-module-is-running
	//   Мы сделаем так: возьмем mutex на загрузку модулей, найдем наш. Не нашли -- вернем
	//   ошибку. Нашли -- увеличиваем refcount (счетчик ссылок) c помощью
	//   try_module_get, пока держим mutex. Вроде, try_module_get не берет
	//   этот mutex внутри себя. Он буквально идет в структуру и увеличивает
	//   atomic с количеством ссылок.
	mutex_lock(&module_mutex);

	struct module* mod = find_module("phonebook");
	if (mod == NULL) {
		mutex_unlock(&module_mutex);
		return NULL;
	}

	if (!try_module_get(mod)) {
		// Видимо, модуль в текущий момент выгружается.
		mutex_unlock(&module_mutex);
		return NULL;
	}

	// Модуль уже будет жить, мы взяли на него ссылку, refcount не станет
	//   нулевым из-за нашей ссылки. Отпускаем mutex, вернем ссылку.
	mutex_unlock(&module_mutex);
	return mod;
}

SYSCALL_DEFINE1(add_user, struct pb_user_data* __user, ud) {
	struct module* pb_mod = find_pb_module();
	if (pb_mod == NULL) {
		return -ENODEV;
	}

	// Поиск по kernel/module/kallsyms.c дал результаты, удалось найти функцию,
	//   которая ищет в модуле, если у нас есть struct module*, заданный
	//   символ.
	// Значением символа с названием функции будет ее адрес в памяти,
	//   занятой модулем после загрузки.
	pb_add_user_t func = (pb_add_user_t) find_kallsyms_symbol_value(pb_mod, "pb_add_user");
	if (func == NULL) {
		// Мы "брали" модуль (увеличивали счетчик ссылок), теперь положим,
		//   чтобы его могли выгрузить при необходимости.
		module_put(pb_mod);
		return -ENODEV;
	}

	struct pb_user_data* copied_ud = kzalloc(sizeof(struct pb_user_data), GFP_KERNEL_ACCOUNT);
	if (copied_ud == NULL) {
		module_put(pb_mod);
		return -ENOMEM;
	}

	unsigned long error_count = copy_from_user(copied_ud, ud, sizeof(*copied_ud));
	if (error_count != 0) {
		kfree(copied_ud);
		module_put(pb_mod);
		return -EFAULT;
	}

	long result = func(copied_ud);

	kfree(copied_ud);
	module_put(pb_mod);
	return result;
}

SYSCALL_DEFINE3(get_user, const char* __user, last_name, unsigned int, len, struct pb_user_data* __user, ud) {
	struct module* pb_mod = find_pb_module();
	if (pb_mod == NULL) {
		return -ENODEV;
	}

	// Поиск по kernel/module/kallsyms.c дал результаты, удалось найти функцию,
	//   которая ищет в модуле, если у нас есть struct module*, заданный
	//   символ.
	// Значением символа с названием функции будет ее адрес в памяти,
	//   занятой модулем после загрузки.
	pb_get_user_t func = (pb_get_user_t) find_kallsyms_symbol_value(pb_mod, "pb_get_user");
	if (func == NULL) {
		// Мы "брали" модуль (увеличивали счетчик ссылок), теперь положим,
		//   чтобы его могли выгрузить при необходимости.
		module_put(pb_mod);
		return -ENODEV;
	}

	char* copied_last_name = kzalloc(len, GFP_KERNEL_ACCOUNT);
	if (copied_last_name == NULL) {
		module_put(pb_mod);
		return -ENOMEM;
	}

	if (len == 0) {
		// Copy to user returns number of bytes not copied.
		//   Which means on success it returns 0.
		//   But if the length is zero, then there should be
		//   no errors, it seems. Let's check for this.
		//   Also my code elsewhere should check for it, but
		//   I didn't think about this case before. Like I was
		//   writing a general and more practical case :)
		//   We can always add some defense programming in play :)
		kfree(copied_last_name);
		module_put(pb_mod);
		return -EINVAL;
	}

	unsigned long error_count = copy_from_user(copied_last_name, last_name, len);
	if (error_count != 0) {
		kfree(copied_last_name);
		module_put(pb_mod);
		return -EFAULT;
	}

	struct pb_user_data* ud_to_copy = kzalloc(sizeof(struct pb_user_data), GFP_KERNEL_ACCOUNT);
	if (ud_to_copy == NULL) {
		kfree(copied_last_name);
		module_put(pb_mod);
		return -ENOMEM;
	}

	long result = func(copied_last_name, len, ud_to_copy);

	error_count = copy_to_user(ud, ud_to_copy, sizeof(*ud_to_copy)); 
	if (error_count != 0) {
		kfree(copied_last_name);
		kfree(ud_to_copy);
		module_put(pb_mod);
		return -EFAULT;
	}

	kfree(copied_last_name);
	kfree(ud_to_copy);
	module_put(pb_mod);
	return result;
}

SYSCALL_DEFINE2(del_user, const char* __user, last_name, unsigned int, len) {
	struct module* pb_mod = find_pb_module();
	if (pb_mod == NULL) {
		return -ENODEV;
	}

	// Поиск по kernel/module/kallsyms.c дал результаты, удалось найти функцию,
	//   которая ищет в модуле, если у нас есть struct module*, заданный
	//   символ.
	// Значением символа с названием функции будет ее адрес в памяти,
	//   занятой модулем после загрузки.
	pb_del_user_t func = (pb_del_user_t) find_kallsyms_symbol_value(pb_mod, "pb_del_user");
	if (func == NULL) {
		// Мы "брали" модуль (увеличивали счетчик ссылок), теперь положим,
		//   чтобы его могли выгрузить при необходимости.
		module_put(pb_mod);
		return -ENODEV;
	}

	char* copied_last_name = kzalloc(len, GFP_KERNEL_ACCOUNT);
	if (copied_last_name == NULL) {
		module_put(pb_mod);
		return -ENOMEM;
	}

	if (len == 0) {
		// Copy to user returns number of bytes not copied.
		//   Which means on success it returns 0.
		//   But if the length is zero, then there should be
		//   no errors, it seems. Let's check for this.
		//   Also my code elsewhere should check for it, but
		//   I didn't think about this case before. Like I was
		//   writing a general and more practical case :)
		//   We can always add some defense programming in play :)
		kfree(copied_last_name);
		module_put(pb_mod);
		return -EINVAL;
	}

	unsigned long error_count = copy_from_user(copied_last_name, last_name, len);
	if (error_count != 0) {
		kfree(copied_last_name);
		module_put(pb_mod);
		return -EFAULT;
	}

	long result = func(copied_last_name, len);

	kfree(copied_last_name);
	module_put(pb_mod);
	return result;
}

#else

SYSCALL_DEFINE1(add_user, struct pb_user_data* __user, ud) {
	(void) ud;
	return -ENODEV;
}

SYSCALL_DEFINE3(get_user, const char* __user, last_name, unsigned int, len, struct pb_user_data* __user, ud) {
	(void) last_name;
	(void) len;
	(void) ud;
	return -ENODEV;
}

SYSCALL_DEFINE2(del_user, const char* __user, last_name, unsigned int, len) {
	(void) last_name;
	(void) len;
	return -ENODEV;
}

#endif
