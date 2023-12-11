#
# This Makefile attempts to build OpenConnect and its dependencies for Android
#
# It doesn't do a stunning job of tracking changes in the dependencies and
# automatically rebuilding them, but it's good enough for getting them built
# and installed into its own local sysroot.
#
# As long as you have the Android NDK toolchain on your path, you should then
# be able to edit fairly much anything in place and rebuild it locally.
#
# It should also be fairly simple to extend this to cross-compile for any target

# Last tested with https://dl.google.com/android/repository/android-ndk-r21b-linux-x86_64.zip


NDK     := /home/ramin/android/sdk/ndk/25.0.8775105
ARCH    := x86_64
API_LEVEL := 23
STRIP := llvm-strip

# You should be able to just 'make ARCH=x86' and it should DTRT.
ifeq ($(ARCH),arm)
TRIPLET := armv7a-linux-androideabi
endif
ifeq ($(ARCH),arm64)
TRIPLET := aarch64-linux-android
API_LEVEL := 26
endif
ifeq ($(ARCH),x86)
TRIPLET := i686-linux-android
endif
ifeq ($(ARCH),x86_64)
TRIPLET := x86_64-linux-android
endif

TOPDIR := $(shell pwd)
DESTDIR := $(TOPDIR)/$(TRIPLET)/out

HOST_TAG= linux-$(shell uname -m)
TOOLCHAIN := $(NDK)/toolchains/llvm/prebuilt/$(HOST_TAG)
TOOLCHAIN_OPTS := -target=$(TRIPLET)
PATH := $(TOOLCHAIN)/bin:$(PATH)
OC_SYSROOT := $(TOOLCHAIN)/sysroot/usr
PKG_CONFIG_LIBDIR := $(OC_SYSROOT)/lib/pkgconfig

export PATH PKG_CONFIG_LIBDIR

# PKG_CONFIG_LIBDIR gets exported to sub-makes, but not to $(shell
PKG_CONFIG := PKG_CONFIG_LIBDIR=$(PKG_CONFIG_LIBDIR) pkg-config

MAKEINSTALL=$(MAKE) INSTALL=$(TOPDIR)/install_symlink.sh
FETCH=$(TOPDIR)/fetch.sh

CONFIGURE_ARGS := --host=$(TRIPLET) --prefix=$(OC_SYSROOT) \
		  --disable-shared --enable-static --with-pic \
		  CC=$(TRIPLET)$(API_LEVEL)-clang CFLAGS="$(EXTRA_CFLAGS)"

SOURCE_LIST = $(LIBXML2_SRC)/configure $(GMP_SRC)/configure \
	$(NETTLE_SRC)/configure $(GNUTLS_SRC)/configure \
	$(STOKEN_SRC)/configure \
	$(LZ4_DIR)/Makefile

PKG_LIST := LIBXML2 GMP NETTLE GNUTLS STOKEN LZ4

MIRROR_TEST_TARGETS := $(addprefix mirror-test-,$(PKG_LIST))

all: openconnect run_pie

#####################################################################
#
# Build libxml2 with minimal configuration for OpenConnect
#
# http://xmlsoft.org/news.html
LIBXML2_VER := 2.9.11
LIBXML2_TAR := libxml2-$(LIBXML2_VER).tar.gz
LIBXML2_SHA := 886f696d5d5b45d780b2880645edf9e0c62a4fd6841b853e824ada4e02b4d331
LIBXML2_SRC := sources/libxml2-$(LIBXML2_VER)
LIBXML2_BUILD := $(TRIPLET)/libxml2

$(LIBXML2_TAR):
	$(FETCH) $@ $(LIBXML2_SHA)

$(LIBXML2_SRC)/configure: $(LIBXML2_TAR)
	mkdir -p sources
	tar xfz $<  -C sources
	touch $@

$(LIBXML2_BUILD)/Makefile: $(LIBXML2_SRC)/configure
	mkdir -p $(LIBXML2_BUILD)
	cd $(LIBXML2_BUILD) && ../../$(LIBXML2_SRC)/configure $(CONFIGURE_ARGS) \
	    --without-c14n -without-catalog --without-debug --without-docbook \
	    --without-fexceptions --without-ftp --without-history \
	    --without-http --without-iconv --without-iconv \
	    --without-iso8859x --without-legacy --without-pattern \
	    --without-push --without-regexps --without-run-debug \
	    --without-sax1 --without-schemas --without-schematron \
	    --without-threads --without-valid --without-xinclude \
	    --without-xpath --without-xptr --without-zlib --without-lzma \
	    --without-coverage --without-python

$(LIBXML2_BUILD)/libxml2.la: $(LIBXML2_BUILD)/Makefile
	$(MAKE) -C $(LIBXML2_BUILD) libxml2.la

$(LIBXML2_BUILD)/libxml-2.0.pc: $(LIBXML2_BUILD)/Makefile
	$(MAKE) -C $(LIBXML2_BUILD) libxml-2.0.pc

$(OC_SYSROOT)/lib/libxml2.la: $(LIBXML2_BUILD)/libxml2.la
	$(MAKEINSTALL) -C $(LIBXML2_BUILD) install-libLTLIBRARIES

$(OC_SYSROOT)/lib/pkgconfig/libxml-2.0.pc: $(LIBXML2_BUILD)/libxml-2.0.pc
	$(MAKEINSTALL) -C $(LIBXML2_BUILD) install-data

LIBXML_DEPS := $(OC_SYSROOT)/lib/libxml2.la $(OC_SYSROOT)/lib/pkgconfig/libxml-2.0.pc

libxml: $(LIBXML_DEPS)


#####################################################################
#
# Build GNU MP
#
# https://gmplib.org/
GMP_VER := 6.2.1
GMP_TAR := gmp-$(GMP_VER).tar.xz
GMP_SHA := fd4829912cddd12f84181c3451cc752be224643e87fac497b69edddadc49b4f2
GMP_SRC := sources/gmp-$(GMP_VER)
GMP_BUILD := $(TRIPLET)/gmp

$(GMP_TAR):
	$(FETCH) $@ $(GMP_SHA)

$(GMP_SRC)/configure: $(GMP_TAR)
	mkdir -p sources
	tar -xJf $< -C sources
	touch $@

$(GMP_BUILD)/Makefile: $(GMP_SRC)/configure
	mkdir -p $(GMP_BUILD)
	cd $(GMP_BUILD) && ../../$(GMP_SRC)/configure $(CONFIGURE_ARGS)


$(GMP_BUILD)/libgmp.la: $(GMP_BUILD)/Makefile
	$(MAKE) -C $(GMP_BUILD)

$(OC_SYSROOT)/lib/libgmp.la: $(GMP_BUILD)/libgmp.la
	$(MAKEINSTALL) -C $(GMP_BUILD) install

GMP_DEPS := $(OC_SYSROOT)/lib/libgmp.la

gmp: $(GMP_DEPS)


#####################################################################
#
# Build nettle
#
# https://ftp.gnu.org/gnu/nettle/
NETTLE_VER := 3.6
NETTLE_TAR := nettle-$(NETTLE_VER).tar.gz
NETTLE_SHA := d24c0d0f2abffbc8f4f34dcf114b0f131ec3774895f3555922fe2f40f3d5e3f1
NETTLE_SRC := sources/nettle-$(NETTLE_VER)
NETTLE_BUILD := $(TRIPLET)/nettle

$(NETTLE_TAR):
	$(FETCH) $@ $(NETTLE_SHA)

$(NETTLE_SRC)/configure: $(NETTLE_TAR)
	mkdir -p sources
	tar xfz $< -C sources
	touch $@

$(NETTLE_BUILD)/Makefile: $(NETTLE_SRC)/configure $(GMP_DEPS)
	mkdir -p $(NETTLE_BUILD)
	cd $(NETTLE_BUILD) && ../../$(NETTLE_SRC)/configure $(CONFIGURE_ARGS)

$(NETTLE_BUILD)/libnettle.a: $(NETTLE_BUILD)/Makefile
	$(MAKE) -C $(NETTLE_BUILD) SUBDIRS=

$(OC_SYSROOT)/lib/libnettle.a: $(NETTLE_BUILD)/libnettle.a
	$(MAKEINSTALL) -C $(NETTLE_BUILD) SUBDIRS= install

NETTLE_DEPS := $(OC_SYSROOT)/lib/libnettle.a

nettle: $(NETTLE_DEPS)


#####################################################################
#
# Build GnuTLS
#
# https://www.gnutls.org/download.html
GNUTLS_VER := 3.8.1
GNUTLS_TAR := gnutls-$(GNUTLS_VER).tar.xz
GNUTLS_SHA := ba8b9e15ae20aba88f44661978f5b5863494316fe7e722ede9d069fe6294829c
GNUTLS_SRC := sources/gnutls-$(GNUTLS_VER)
GNUTLS_BUILD := $(TRIPLET)/gnutls

$(GNUTLS_TAR):
	$(FETCH) $@ $(GNUTLS_SHA)

$(GNUTLS_SRC)/configure: $(GNUTLS_TAR)
	mkdir -p sources
	xz -d < $< | tar xf - -C sources
	touch $@

#$(GNUTLS_SRC)/configure.ac:
#	mkdir -p sources
#	cd sources && git clone git://gitorious.org/gnutls/gnutls.git

#$(GNUTLS_SRC)/configure: $(GNUTLS_SRC)/configure.ac
#	touch $(GNUTLS_SRC)/ChangeLog
#	cd $(GNUTLS_SRC) && autoreconf -fvi

$(GNUTLS_BUILD)/Makefile: $(GNUTLS_SRC)/configure $(NETTLE_DEPS)
	mkdir -p $(GNUTLS_BUILD)
	cd $(GNUTLS_BUILD) && ../../$(GNUTLS_SRC)/configure $(CONFIGURE_ARGS) \
		AUTOGEN=/bin/false \
		--disable-threads --disable-tests --disable-nls \
		--disable-doc --disable-openssl-compatibility --disable-cxx \
		--disable-openssl-compatibility --disable-ocsp --disable-tools \
		--disable-anon-authentication --with-included-libtasn1 \
		--enable-psk-authentication --disable-srp-authentication \
		--disable-dtls-srtp-support  --enable-dhe --enable-ecdhe \
		--with-included-unistring --without-p11-kit --disable-guile

$(GNUTLS_BUILD)/lib/libgnutls.la: $(GNUTLS_BUILD)/Makefile
	$(MAKE) -C $(GNUTLS_BUILD)

$(OC_SYSROOT)/lib/libgnutls.la: $(GNUTLS_BUILD)/lib/libgnutls.la
	$(MAKEINSTALL) -C $(GNUTLS_BUILD) install

GNUTLS_DEPS := $(OC_SYSROOT)/lib/libgnutls.la

gnutls: $(GNUTLS_DEPS)


#####################################################################
#
# Build libstoken
#
# https://sourceforge.net/projects/stoken/files/
STOKEN_VER := 0.92
STOKEN_TAR := stoken-$(STOKEN_VER).tar.gz
STOKEN_SHA := aa2b481b058e4caf068f7e747a2dcf5772bcbf278a4f89bc9efcbf82bcc9ef5a
STOKEN_SRC := sources/stoken-$(STOKEN_VER)
STOKEN_BUILD := $(TRIPLET)/stoken

$(STOKEN_TAR):
	$(FETCH) $@ $(STOKEN_SHA)

$(STOKEN_SRC)/configure: $(STOKEN_TAR)
	mkdir -p sources
	tar xfz $< -C sources
	touch $@

$(STOKEN_BUILD)/Makefile: $(STOKEN_SRC)/configure $(NETTLE_DEPS)
	mkdir -p $(STOKEN_BUILD)
	cd $(STOKEN_BUILD) && ../../$(STOKEN_SRC)/configure $(CONFIGURE_ARGS) \
		--without-gtk

$(STOKEN_BUILD)/libstoken.la: $(STOKEN_BUILD)/Makefile
	$(MAKE) -C $(STOKEN_BUILD)

$(OC_SYSROOT)/lib/libstoken.la: $(STOKEN_BUILD)/libstoken.la
	$(MAKEINSTALL) -C $(STOKEN_BUILD) install

STOKEN_DEPS := $(OC_SYSROOT)/lib/libstoken.la

stoken: $(STOKEN_DEPS)


#####################################################################
#
# Build liblz4
#
# https://github.com/lz4/lz4/tags
LZ4_VER := 1.9.3
LZ4_TAR := lz4-v$(LZ4_VER).tar.gz
LZ4_SHA := 030644df4611007ff7dc962d981f390361e6c97a34e5cbc393ddfbe019ffe2c1
LZ4_DIR := $(TRIPLET)/lz4-$(LZ4_VER)

$(LZ4_TAR):
	$(FETCH) $@ $(LZ4_SHA)

$(LZ4_DIR)/Makefile: $(LZ4_TAR)
	mkdir -p $(TRIPLET)
	tar xzf $< -C $(TRIPLET)
	touch $@

$(OC_SYSROOT)/lib/liblz4.a: $(LZ4_DIR)/Makefile
	$(MAKE) -C $(LZ4_DIR)/lib \
		CC="$(TRIPLET)$(API_LEVEL)-clang $(EXTRA_CFLAGS)" \
		AR="llvm-ar" \
		LIBDIR=$(OC_SYSROOT)/lib \
		INCLUDEDIR=$(OC_SYSROOT)/include \
		install
	rm -f $(OC_SYSROOT)/lib/liblz4.so*

LZ4_DEPS := $(OC_SYSROOT)/lib/liblz4.a

lz4: $(LZ4_DEPS)

#####################################################################
#
# Build OpenConnect for Android
#
OPENCONNECT_SRC := ..
OPENCONNECT_BUILD := $(TRIPLET)/openconnect

$(OPENCONNECT_SRC)/configure:
	cd $(OPENCONNECT_SRC) && ./autogen.sh

$(OPENCONNECT_BUILD)/Makefile: $(GNUTLS_DEPS) $(LIBXML_DEPS) \
		$(STOKEN_DEPS) $(LZ4_DEPS) $(OPENCONNECT_SRC)/configure
	mkdir -p $(OPENCONNECT_BUILD)
	cd $(OPENCONNECT_BUILD) && ../../../configure \
	--host=$(TRIPLET) --prefix=$(OC_SYSROOT) \
	STRIP=$(STRIP) \
	CC=$(TRIPLET)$(API_LEVEL)-clang \
	CFLAGS="$(EXTRA_CFLAGS) -fvisibility=default " \
	LDFLAGS="$(EXTRA_LDFLAGS) -rdynamic -pie" \
	GNUTLS_LIBS="$(shell $(PKG_CONFIG) --static --libs gnutls)" \
	LIBSTOKEN_LIBS="$(shell $(PKG_CONFIG) --static --libs stoken)" \
	--with-vpnc-script=/etc/vpnc/vpnc-script \
	--with-java=$(OC_SYSROOT)/include --enable-jni-standalone \
	--disable-symvers

openconnect: $(OPENCONNECT_BUILD)/Makefile
	make -C $(OPENCONNECT_BUILD)
	make -C $(OPENCONNECT_BUILD) install-strip DESTDIR=$(DESTDIR)


#####################################################################
#
# Build run_pie helper program
#
$(DESTDIR)/sbin/run_pie: run_pie.c
	echo $(TOPDIR)
	mkdir -p $(DESTDIR)/sbin
	$(TRIPLET)$(API_LEVEL)-clang $< -o $@ -ldl

.PHONY: run_pie
run_pie: $(DESTDIR)/sbin/run_pie


#####################################################################
#
# Special targets for maintainer use
#

# download + extract, but do not build
.PHONY: sources
sources: $(SOURCE_LIST)

.PHONY: $(MIRROR_TEST_TARGETS)
$(MIRROR_TEST_TARGETS) : mirror-test-% :
	$(FETCH) --mirror-test $($(*)_TAR) $($(*)_SHA)

# (re)test all mirrors for all packages. safe for use with "make -jN"
.PHONY: mirror-test
mirror-test: $(MIRROR_TEST_TARGETS)
