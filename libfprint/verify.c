#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fprint.h"
#include "fio_cli.h"
#include "http.h"

static void on_http_request(http_s *h);
static void on_http_response(http_s *h);


struct fp_print_data *data;
int userId;

struct fp_dscv_dev *discover_device(struct fp_dscv_dev **discovered_devs)
{
	struct fp_dscv_dev *ddev = discovered_devs[0];
	struct fp_driver *drv;
	if (!ddev)
		return NULL;
	
	drv = fp_dscv_dev_get_driver(ddev);
	fprintf(stdout, "Found device claimed by %s driver\n", fp_driver_get_full_name(drv));
	return ddev;
}

int verify(struct fp_dev *dev, struct fp_print_data *data)
{
	int r;

	do {
		struct fp_img *img = NULL;

		sleep(1);
		fprintf(stdout, "Waiting fingerprint scan...\n");
		r = fp_verify_finger_img(dev, data, &img);
		/*if (img) {
			fp_img_save_to_file(img, "verify.pgm");
			printf("Wrote scanned image to verify.pgm\n");
			fp_img_free(img);
		}*/
		if (r < 0) {
			fprintf(stderr, "Verification failed with error %d\n", r);
			return r;
		}
		switch (r) {
		case FP_VERIFY_NO_MATCH:
			fprintf(stdout, "NO MATCH!\n");
			return 0;
		case FP_VERIFY_MATCH:
			fprintf(stdout, "MATCH!\n");
			return 0;
		case FP_VERIFY_RETRY:
			fprintf(stdout, "Scan didn't quite work. Please try again.\n");
			break;
		case FP_VERIFY_RETRY_TOO_SHORT:
			fprintf(stdout, "Swipe was too short, please try again.\n");
			break;
		case FP_VERIFY_RETRY_CENTER_FINGER:
			fprintf(stdout, "Please center your finger on the sensor and try again.\n");
			break;
		case FP_VERIFY_RETRY_REMOVE_FINGER:
			fprintf(stdout, "Please remove finger from the sensor and try again.\n");
			break;
		}
	} while (1);
}

int startVerification()
{
	int r = 1;
	struct fp_dscv_dev *ddev;
	struct fp_dscv_dev **discovered_devs;
	struct fp_dev *dev;

	//setenv ("G_MESSAGES_DEBUG", "all", 0);
	//setenv ("LIBUSB_DEBUG", "3", 0);

	r = fp_init();
	if (r < 0) {
		fprintf(stderr, "Failed to initialize libfprint\n");
		exit(1);
	}

	discovered_devs = fp_discover_devs();
	if (!discovered_devs) {
		fprintf(stderr, "Could not discover devices\n");
		goto out;
	}

	ddev = discover_device(discovered_devs);
	if (!ddev) {
		fprintf(stderr, "No devices detected.\n");
		goto out;
	}

	dev = fp_dev_open(ddev);
	fp_dscv_devs_free(discovered_devs);
	if (!dev) {
		fprintf(stderr, "Could not open device.\n");
		goto out;
	}

	printf("Opened device. Loading previously enrolled right index finger "
		"data...\n");
		
	if (!data) {
		fprintf(stderr, "Failed to load fingerprint, error %d\n", r);
		fprintf(stderr, "Did you remember to enroll your right index finger "
			"first?\n");
		goto out_close;
	}

	fprintf(stdout, "Print loaded, starting verification...\n");
	
	verify(dev, data);

	fp_print_data_free(data);
out_close:
	fp_dev_close(dev);
out:
	fp_exit();
	return 0;
}

static void on_http_request(http_s *h) {
  
  http_parse_body(h);
  FIOBJ obj = h->body;
  
  FIOBJ key = fiobj_str_new("parameter", 9);
  fio_str_info_s param;
  
  if (FIOBJ_TYPE_IS(obj, FIOBJ_T_HASH) // make sure the JSON object is a Hash
      && fiobj_hash_get(obj, key)) {   // test for the existence of the key
        param = fiobj_obj2cstr(fiobj_hash_get(obj, key));
        userId = atoi(param.data);
        fprintf(stdout, "Requested verification for user ID %d\n", userId);
  }else{
	  
	  fprintf(stderr,"Error parsing json message from webserver. No parameter data found.\n");
	  http_send_body(h, "ERROR", 2);
	  return;
  }
  
  fiobj_free(key);
  
  http_send_body(h, "OK", 2);
  
  
  char* ip = getenv("WEBSERVER");
  char* port = getenv("WEBSERVER_PORT");
  char header[1024];
  
  sprintf(header, "%s:%s/getFingerprint?userId=%d", ip, port, userId);
  
  http_connect(header, NULL, .on_response = on_http_response);
  
  
}

static void on_http_response(http_s *h) {
	
  if(h->status_str == FIOBJ_INVALID){
	  //ignore the first empty response
	  http_finish(h);
	  return;
  }
  
  http_parse_body(h);
  FIOBJ obj = h->body;
  fio_str_info_s raw = fiobj_obj2cstr(obj);
  fprintf(stdout, "Fingerprint received - size = %d\n", raw.len);
  
  data = fp_print_data_from_data((unsigned char *)raw.data, raw.len);
  
  if(!data)
	fprintf(stderr, "Fingerprint data conversion failed!\n");
  
  http_send_body(h, "OK", 2);

  fiobj_free(obj);

  startVerification();
}

int main() {
  
  //spring boot IP and port
  char* ip = getenv("WEBSERVER");
  char* port = getenv("WEBSERVER_PORT");
  
  if(strlen(ip) == 0 || strlen(port) == 0){
    fprintf(stderr, "Environment variables WEBSERVER and WEBSERVER_PORT not set.\n");
    return 1;
  } else{
    fprintf(stdout, "WEBSERVER=%s, WEBSERVER_PORT=%s\n", ip, port);
  }

  if (http_listen("3000", NULL,
                  .on_request = on_http_request) == -1) {
    fprintf(stderr, "facil.io couldn't initialize HTTP service (already running?)\n");
    return 1;
  }
  
  fio_start(.threads = 1, .workers = 1);
  fio_cli_end();
  return 0;
}

