%define with_systemd_rpm_macros 1

%if 0%{?rhel}
%define with_systemd_rpm_macros 0
%endif

Name:		phpturd
Version:	1.0.1
Release:	1%{?dist}
Summary:	PHP turd interception library
License:	GPLv2+
URL:		https://github.com/unipartdigital/phpturd
Source0:	%{name}-%{version}.tar.gz
BuildRequires:	autoconf
BuildRequires:	automake
BuildRequires:	libtool
BuildRequires:	gcc
BuildRequires:	libselinux-devel
Provides:	libphpturd = %{version}-%{release}

%if 0%{?with_systemd_rpm_macros}
BuildRequires:	systemd-rpm-macros
%endif

%description
An LD_PRELOAD library that allows for incompetently written PHP code
(such as SuiteCRM) to be forcibly divided into two top-level "turd"
directories: a distribution tree (where permissions should be set as
read-only) and a writable scratch area.

%prep
%autosetup

%build
./autogen.sh
%configure
%make_build

%install
%make_install
rm -f %{buildroot}%{_libdir}/*.la
install -D -m 644 phpturd.conf \
	%{buildroot}%{_unitdir}/php-fpm.service.d/%{name}.conf

%files
%doc README.md
%license COPYING
%{_libdir}/libphpturd.so
%{_libdir}/libphpturd.so.*
%{_unitdir}/php-fpm.service.d/%{name}.conf

%changelog
* Sun May 17 2020 Michael Brown <mbrown@fensystems.co.uk> 1.0.1-1
- turd: Provide wrapper for opendir()

* Sat May 16 2020 Michael Brown <mbrown@fensystems.co.uk> 1.0.0-1
- test: Add test for PHP's tempnam() function
- turd: Provide wrappers for mktemp() and friends

* Sat May 16 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.8-1
- test: Add a test case for implicit directory creation
- turd: Allow intermediate directories to be created transparently
- turd: Ensure that all library function prototypes are included
- turd: Include fcntl.h to get the prototype for open()
- turd: Avoid unnecessary string duplication
- turd: Replace all uses of strcpy()

* Sat May 16 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.7-1
- turd: Use memcpy() to copy fixed-length unterminated strings

* Fri May 15 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.6-1
- build: Fix building on CentOS

* Fri May 15 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.5-1
- build: Add missing BuildRequires for systemd-rpm-macros

* Fri May 15 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.4-1
- build: Create a GitHub release for tagged commits

* Fri May 15 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.3-1
- build: Bump configure.ac version automatically via tito
- doc: Update README.md to include installation instructions
- build: Include php-fpm.service.d/phpturd.conf in RPM package
- build: Include README.md and phpturd.spec in source distribution
- test: Add test cases using a variety of PHP file-access functions
- turd: Include wrapper for fopen()
- turd: Allow for non-integer return types from wrapped functions
- test: Run tests via GitHub workflow
- test: Add sketch self-test framework
- turd: Add hopefully complete list of remaining library call wrappers
- doc: Suggest using name-only form for LD_PRELOAD
- build: Add a Provides tag for libphpturd

* Fri May 15 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.2-1
- build: Add missing RPM BuildRequires

* Fri May 15 2020 Michael Brown <mbrown@fensystems.co.uk> 0.0.1-1
- First packaged version
