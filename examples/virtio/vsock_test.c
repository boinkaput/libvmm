#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/vm_sockets.h>
#include <unistd.h>

int main()
{
	int s = socket(AF_VSOCK, SOCK_STREAM, 0);

	struct sockaddr_vm addr;
	memset(&addr, 0, sizeof(struct sockaddr_vm));
	addr.svm_family = AF_VSOCK;
	addr.svm_port = 9999;
	addr.svm_cid = VMADDR_CID_HOST;

	int r = connect(s, &addr, sizeof(struct sockaddr_vm));
	if (r < 0) {
		printf("VSOCK TEST|ERROR: connect failed with '%s'\n", strerror(errno));
		return 1;
	}

	r = send(s, "Hello, world!", 13, 0);
	if (r < 0) {
		printf("VSOCK TEST|ERROR: send failed with '%s'\n", strerror(errno));
		return 1;
	}

	close(s);

	return 0;
}

