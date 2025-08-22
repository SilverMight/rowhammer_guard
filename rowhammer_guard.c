#include <linux/module.h> 
#include <linux/kernel.h>

static int __init rowhammer_guard_init(void) 
{ 

    pr_info("rowhammer_guard: module loaded\n"); 
    return 0; 

} 

static void __exit rowhammer_guard_exit(void) 
{ 

    pr_info("rowhammer_guard: module unloaded"); 
} 

 
module_init(rowhammer_guard_init);
module_exit(rowhammer_guard_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SD Group 5 Kernel Hacking Group");
MODULE_DESCRIPTION("Rowhammer detecting and mitigation module.");
MODULE_VERSION("0.1");
