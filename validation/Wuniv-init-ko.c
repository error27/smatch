struct s {
	void *ptr;
};


static struct s s = { 0 };

/*
 * check-name: univ-init-ko
 *
 * check-error-start
Wuniv-init-ko.c:6:23: warning: Using plain integer as NULL pointer
 * check-error-end
 */
