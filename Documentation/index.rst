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

You can find released versions of sparse at http://www.kernel.org/pub/software/devel/sparse/dist/

Obtaining sparse via Git
~~~~~~~~~~~~~~~~~~~~~~~~

Sparse uses the `Git version control system <http://git-scm.com/>`_. You can obtain the most recent version of sparse directly from the Git repository with the command::

	git clone git://git.kernel.org/pub/scm/devel/sparse/sparse.git

You can also `browse the Git repository <https://git.kernel.org/pub/scm/devel/sparse/sparse.git>`_.

Mailing list
~~~~~~~~~~~~

Discussions about sparse occurs on the sparse mailing list, linux-sparse@vger.kernel.org. To subscribe to the list, send an email with ``subscribe linux-sparse`` in the body to ``majordomo@vger.kernel.org``.

You can browse the list archives at https://marc.info/?l=linux-sparse.

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
