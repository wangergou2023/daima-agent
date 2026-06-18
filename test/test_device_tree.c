#include "test_utils.h"
#include "linux/bus.h"
#include "linux/slab.h"
#include "cjson.h"
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

int printk(const char *fmt, ...) { (void)fmt; return 0; }

int of_populate(const char *json_path);

int main(void)
{
	test_begin();

	TEST_CASE("of_populate missing file");
	int count = of_populate("/nonexistent/device_tree.json");
	TEST_ASSERT(count == 0, "missing file returns 0");
	TEST_DONE();

	TEST_CASE("of_populate with data field");
	char tmp_path[256];
	snprintf(tmp_path, sizeof(tmp_path), "/tmp/test_device_tree_%d.json", getpid());

	FILE *f = fopen(tmp_path, "w");
	TEST_ASSERT(f != NULL, "create temp file");

	const char *json =
		"{\"devices\":["
		"  {\"bus\":\"llm_bus\",\"name\":\"test_model\","
		"   \"data\":{\"protocol\":\"openai\",\"health_url\":\"http://127.0.0.1:1/models\"}}"
		"]}";
	fprintf(f, "%s", json);
	fclose(f);

	if (llm_bus) {
		count = of_populate(tmp_path);
		TEST_ASSERT(count == 1, "populated 1 device");

		struct device *dev = bus_find_device(llm_bus, "test_model");
		TEST_ASSERT(dev != NULL, "device found on bus");
		TEST_ASSERT(dev->data != NULL, "data field present");
		if (dev->data) {
			cJSON *parsed = cJSON_Parse((const char *)dev->data);
			TEST_ASSERT(parsed != NULL, "data is valid JSON");
			if (parsed) {
				const char *proto = cJSON_GetStringValue(
					cJSON_GetObjectItem(parsed, "protocol"));
				TEST_ASSERT(proto != NULL, "protocol field");
				TEST_ASSERT(strcmp(proto, "openai") == 0, "protocol=openai");
				cJSON_Delete(parsed);
			}
		}

		/* cleanup */
		if (dev) {
			device_unregister(dev);
			kfree((void *)dev->name);
			kfree(dev->data);
			kfree(dev);
		}
	}
	unlink(tmp_path);
	TEST_DONE();

	TEST_CASE("of_populate skips existing devices");
	f = fopen(tmp_path, "w");
	fprintf(f, "%s",
		"{\"devices\":["
		"  {\"bus\":\"tool_bus\",\"name\":\"weather\"}"
		"]}");
	fclose(f);

	count = of_populate(tmp_path);
	TEST_ASSERT(count == 0, "existing device skipped");
	unlink(tmp_path);
	TEST_DONE();

	test_summary();
	return _test_failed ? 1 : 0;
}
