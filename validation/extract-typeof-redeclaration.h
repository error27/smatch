struct operations {
	int (*call)(int value);
};

extern typeof(*((struct operations *)0)->call) target;
