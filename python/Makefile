PYTHON = python3
MAKE = make

BUILDDIR = build
DOCDIR = doc

SOURCES = netconf.c \
	session.c

all: $(BUILDDIR)

$(BUILDDIR): setup.py $(SOURCES)
	$(PYTHON) ./setup.py build 

.PHONY: doc
doc:
	$(MAKE) html -C $(DOCDIR)

install: $(BUILDDIR) setup.py
	$(PYTHON) ./setup.py install

.PHONY: clean
clean:
	-rm -rf $(BUILDDIR)
	$(MAKE) clean -C $(DOCDIR)

