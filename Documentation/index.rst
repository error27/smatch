.. sparse documentation master file.

Welcome to sparse's documentation
=================================

.. toctree::
   :maxdepth: 1

About Sparse
------------

Sparse, the semantic parser, provides a compiler frontend capable of
parsing most of ANSI C as well as many GCC extensions, and a collection
of sample compiler backends, including a static analyzer also called `sparse`.
Sparse provides a set of annotations designed to convey semantic information
about types, such as what address space pointers point to, or what locks
function acquires or releases.

Linus Torvalds started writing Sparse in 2003, initially targeting issues such
as mixing pointers to user address space and pointers to kernel address space.

Josh Triplett was Sparse's first maintainer in 2006. This role was taken over
by Christopher Li in 2009 and by Luc Van Oostenryck in late 2018.

Getting Sparse
--------------

You can find tarballs of released versions of Sparse at
https://www.kernel.org/pub/software/devel/sparse/dist/.

The most recent version can be obtained directly from the Git
repository with the command::

	git clone git://git.kernel.org/pub/scm/devel/sparse/sparse.git

You can also `browse the Git repository <https://git.kernel.org/pub/scm/devel/sparse/sparse.git>`_
or use the mirror at https://github.com/lucvoo/sparse.

Once you have the sources, to build Sparse and install it in your ~/bin
directory, just do::

	cd sparse
	make
	make install

To install it in another directory, use::

	make PREFIX=<some directory> install

Contributing and reporting bugs
-------------------------------

Submission of patches and reporting of bugs, as well as discussions
related to Sparse, should be done via the mailing list:
linux-sparse@vger.kernel.org.
You do not have to be subscribed to the list to send a message there.
Previous discussions and bug reports are available on the list
archives at https://marc.info/?l=linux-sparse.

To subscribe to the list, send an email with
``subscribe linux-sparse`` in the body to ``majordomo@vger.kernel.org``.

Bugs can also be reported and tracked via the `Linux kernel's bugzilla for sparse
<https://bugzilla.kernel.org/enter_bug.cgi?component=Sparse&product=Tools>`_.

User documentation
------------------
.. toctree::
   :maxdepth: 1

   nocast-vs-bitwise

Developer documentation
-----------------------
.. toctree::
   :maxdepth: 1

   test-suite
   dev-options
   api
   IR
   types

How to contribute
-----------------
.. toctree::
   :maxdepth: 1

   submitting-patches
   TODO

Documentation
-------------
.. toctree::
   :maxdepth: 1

   doc-guide

Release Notes
-------------
.. toctree::
   :maxdepth: 1

   release-notes/index

Indices and tables
==================

* :ref:`genindex`
