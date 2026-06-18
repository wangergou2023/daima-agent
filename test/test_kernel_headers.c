#include "linux/kernel.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/irq.h"
#include "linux/sysctl.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

struct sample_node {
    int value;
    struct list_head link;
};

int main(void)
{
    int values[] = { 1, 2, 3 };
    assert(ARRAY_SIZE(values) == 3);
    assert(round_up(5, 4) == 8);
    assert(min(2, 7) == 2);
    assert(max(2, 7) == 7);

    struct sample_node node;
    assert(container_of(&node.link, struct sample_node, link) == &node);

    LIST_HEAD(head);
    struct sample_node first = { .value = 1 };
    struct sample_node second = { .value = 2 };
    INIT_LIST_HEAD(&first.link);
    INIT_LIST_HEAD(&second.link);
    list_add(&first.link, &head);
    list_add(&second.link, &head);

    struct list_head *pos;
    int sum = 0;
    list_for_each(pos, &head) {
        sum += list_entry(pos, struct sample_node, link)->value;
    }
    assert(sum == 3);

    list_del(&first.link);
    assert(first.link.next == &first.link);
    assert(first.link.prev == &first.link);

    char *buf = kzalloc(8, GFP_KERNEL);
    assert(buf);
    assert(memcmp(buf, "\0\0\0\0\0\0\0\0", 8) == 0);
    kfree(buf);

    assert(strcmp(KERN_ERR, "<3>") == 0);
    assert(irq_init() == 0);
    return 0;
}
