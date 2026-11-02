struct pci_dev;
struct device_pci;
struct device;

struct device_func {
	struct device_pci *(*pci)(struct device *);
};

struct device {
	const struct device_func *func;
};

struct device_pci {
	struct pci_dev *pdev;
};

struct pci_dev *test(struct device *device)
{
	return device->func->pci(device)->pdev;
}

/*
 * check-name: extract a member of the result of an indirect call
 * check-command: extract --func=test $file && gcc -std=gnu11 -Werror -x c -fsyntax-only $file.extract.test
 *
 * check-output-start
extract: wrote extract-member-of-indirect-call.c.extract.test
 * check-output-end
 */
