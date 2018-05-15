How to write sparse documentation
=================================

Introduction
------------


The documentation for Sparse is written in plain text augmented with
either `reStructuredText`_ (.rst) or `MarkDown`_ (.md) markup. These
files can be organized hierarchically, allowing a good structuring
of the documentation.
Sparse uses `Sphinx`_ to format this documentation in several formats,
like HTML or PDF.

All documentation related files are in the ``Documentation/`` directory.
In this directory you can use ``make html`` or ``make man`` to generate
the documentation in HTML or manpage format. The generated files can then
be found in the ``build/`` sub-directory.

The root of the documentation is the file ``index.rst`` which mainly
lists the names of the files with real documentation.

.. _Sphinx: http://www.sphinx-doc.org/
.. _reStructuredText: http://docutils.sourceforge.net/rst.html
.. _MarkDown: https://en.wikipedia.org/wiki/Markdown

Autodoc
-------

.. highlight:: none
.. c:autodoc:: Documentation/sphinx/cdoc.py

