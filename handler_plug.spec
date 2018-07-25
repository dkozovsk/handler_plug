Name:       handler_plug
Version:    0.0.1.1.gce4407c
Release:    1%{?dist}
Summary:    TODO
License:    GPLv3+
URL:        https://github.com/dkozovsk/%{name}
Source0:    %{name}-%{version}.tar.xz

BuildRequires: gcc
BuildRequires: gcc-plugin-devel

%description
TODO

%prep
%setup -q

%build
make

%install
install -m0755 -d $RPM_BUILD_ROOT%{_libdir}
install -m0755 handler_plug.so $RPM_BUILD_ROOT%{_libdir}

%files
%{_libdir}/*.so
