Summary: NETCONF protocol library for NETCONF applications.
Name: libnetconf
Version: 0.5.99
Release: 1
URL: http://www.liberouter.org/
Source: https://www.liberouter.org/repo/SOURCES/%{name}-%{version}-%{release}.tar.gz
Group: Liberouter
License: BSD
Vendor: CESNET, z.s.p.o.
Packager: Michal Vasko <>
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}

BuildRequires: gcc make doxygen pkgconfig 
Provides: libnetconf  libnetconf-notifications

%description
Library provides NETCONF protocol functionality for both client as well as
server side applications. It also handles access to the NETCONF
configuration data repositories.

%package devel
Summary: libnetconf development package
Group: Liberouter
Requires: libnetconf = %{version}-%{release}
Provides: libnetconf-devel  libnetconf-notifications

%description devel
This package contains header files for libnetconf library. Install this package
if you want to develop your own NETCONF application.

%prep
%setup

%build
./configure --prefix=%{_prefix} --libdir=%{_libdir} ;
make
make doc

%install
make DESTDIR=$RPM_BUILD_ROOT install

%post
ldconfig

%files
%{_libdir}/libnetconf.so.*
/etc/dbus-1//system.d/libnetconf.notifications.conf
/var/lib/libnetconf/

%files devel
%{_libdir}/pkgconfig/libnetconf.pc
%{_libdir}/libnetconf.so
%{_libdir}/libnetconf.a
%{_prefix}/include/libnetconf.h
%{_prefix}/include/libnetconf_xml.h
%{_prefix}/include/libnetconf_ssh.h
%{_prefix}/include/libnetconf/*
%{_prefix}/share/libnetconf/doxygen/*
%{_bindir}/lnc-creator
%{_datadir}/libnetconf/templates/*
