Name:           cohort
Version:        0.1.0
Release:        1%{?dist}
Summary:        Power, battery, fan and keyboard settings for Lenovo Legion laptops
License:        GPL-3.0-or-later
URL:            https://github.com/yappologistic/Cohort
Source0:        %{url}/archive/v%{version}/Cohort-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
Requires:       polkit
Requires:       qt6-qtdeclarative
Requires:       qt6-qtsvg

%description
Cohort changes the settings Lenovo's own software changes on Windows, through
the drivers Linux already has: power modes and custom power limits, battery
conservation and rapid charging, always-on USB, Fn lock, fan curves, and the
four-zone keyboard lighting. Cohort is not made or endorsed by Lenovo.

%prep
%autosetup -n Cohort-%{version}

%build
%cmake -G Ninja -DBUILD_TESTING=OFF -DCOHORT_INSTALL_LICENSES=OFF
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.yappologistic.Cohort.desktop
%{_sysconfdir}/xdg/autostart/io.github.yappologistic.Cohort.Agent.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/io.github.yappologistic.Cohort.metainfo.xml

%files
%license LICENSE NOTICE licenses/MaterialSymbols-LICENSE.txt licenses/Sung-MIT.txt
%{_bindir}/cohort
%{_libexecdir}/cohort-helper
%{_datadir}/polkit-1/actions/io.github.yappologistic.Cohort.policy
%{_datadir}/applications/io.github.yappologistic.Cohort.desktop
%{_sysconfdir}/xdg/autostart/io.github.yappologistic.Cohort.Agent.desktop
%{_metainfodir}/io.github.yappologistic.Cohort.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.yappologistic.Cohort.svg

%changelog
* Fri Sep 25 2026 yappologistic <yappologistic@users.noreply.github.com> - 0.1.0-1
- First release
