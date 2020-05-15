Name:		phpturd
Version:	0.0.4
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
