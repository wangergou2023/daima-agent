#include "test_utils.h"
#include "linux/bus.h"
#include "linux/driver.h"
#include "linux/list.h"
#include "linux/slab.h"

#include <stdarg.h>
#include <stdio.h>
int printk(const char *fmt, ...) { (void)fmt; return 0; }

static struct bus_type *test_bus;
static int probe_count;
static int remove_count;

static int test_probe(struct device *dev)
{
	(void)dev;
	probe_count++;
	return 0;
}

static void test_remove(struct device *dev)
{
	(void)dev;
	remove_count++;
}

static int test_probe_fail(struct device *dev)
{
	(void)dev;
	return -1;
}

int main(void)
{
	test_begin();

	TEST_CASE("bus_create");
	test_bus = bus_create("test_bus", NULL);
	TEST_ASSERT(test_bus != NULL, "bus_create");
	TEST_ASSERT(strcmp(test_bus->name, "test_bus") == 0, "bus name");
	TEST_DONE();

	TEST_CASE("driver_register");
	struct driver drv = { .name = "test_drv", .probe = test_probe, .remove = test_remove };
	TEST_ASSERT(driver_register(&drv, test_bus) == 0, "driver_register");
	TEST_ASSERT(drv.bus == test_bus, "driver bus assigned");
	TEST_DONE();

	TEST_CASE("device_register + probe + bind");
	probe_count = 0;
	struct device dev = { .name = "test_drv" };
	TEST_ASSERT(device_register(&dev, test_bus) == 0, "device_register");
	TEST_ASSERT(dev.drv == &drv, "device bound to driver");
	TEST_ASSERT(probe_count == 1, "probe called exactly once");
	TEST_DONE();

	TEST_CASE("bus_find_device");
	TEST_ASSERT(bus_find_device(test_bus, "test_drv") == &dev, "find by name");
	TEST_ASSERT(bus_find_device(test_bus, "nonexistent") == NULL, "nonexistent");
	TEST_DONE();

	TEST_CASE("bus_device_exists");
	TEST_ASSERT(bus_device_exists(test_bus, "test_drv") == 1, "exists");
	TEST_ASSERT(bus_device_exists(test_bus, "nonexistent") == 0, "not exist");
	TEST_DONE();

	TEST_CASE("second device with same name skipped");
	probe_count = 0;
	struct device dev2 = { .name = "test_drv" };
	struct driver drv2 = { .name = "test_drv", .probe = test_probe };
	driver_register(&drv2, test_bus);

	TEST_ASSERT(device_register(&dev2, test_bus) == 0, "register duplicate");
	TEST_ASSERT(dev2.drv != NULL, "bound to some driver");
	TEST_ASSERT(probe_count == 1, "probe called once (first match wins)");
	TEST_DONE();

	TEST_CASE("probe failure leaves device unbound");
	struct driver fail_drv = { .name = "fail_drv", .probe = test_probe_fail };
	driver_register(&fail_drv, test_bus);
	struct device fail_dev = { .name = "fail_drv" };
	TEST_ASSERT(device_register(&fail_dev, test_bus) == 0, "register fail device");
	TEST_ASSERT(fail_dev.drv == NULL, "device unbound on probe failure");
	TEST_DONE();

	TEST_CASE("driver_unregister calls remove");
	remove_count = 0;
	driver_unregister(&drv);
	TEST_ASSERT(remove_count == 1, "remove called on unregister");
	TEST_ASSERT(dev.drv == NULL, "device unbound");
	TEST_DONE();

	bus_destroy(test_bus);
	test_summary();
	return _test_failed ? 1 : 0;
}
