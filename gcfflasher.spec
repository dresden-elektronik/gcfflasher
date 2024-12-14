Name:		gcfflasher
Version:	4.5.2
Release:	1%{?dist}
Summary:	Updater for the firmware of Zigbee devices
License:	BSD
URL:		https://github.com/dresden-elektronik/gcfflasher/
Suggests:	%{name}-old-devices
Source0:	%{name}-%{version}.tar.gz
BuildRequires:	cmake gcc-c++
BuildRequires:	pkgconfig(libgpiod)

%description
GCFFlasher is the tool to program the firmware of dresden elektronik
Zigbee products.

%package old-devices
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	libgpiod
Summary:	Add support for older devices
%description old-devices
Older devices are only supported using libgpiod via dlopen().

%prep
%autosetup

%build
%cmake
%cmake_build

%install
%cmake_install

%files
%doc README.md
%license LICENSE.txt
%{_bindir}/GCFFlasher

%files old-devices
