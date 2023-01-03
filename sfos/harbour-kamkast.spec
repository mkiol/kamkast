Name: harbour-kamkast

# >> macros
# << macros

Summary:    Kamkast
Version:    1.0.0
Release:    1
Group:      Application
License:    LICENSE
URL:        http://mozilla.org/MPL/2.0/
Source0:    %{name}-%{version}.tar.bz2
Requires:       sailfishsilica-qt5 >= 0.10.9
BuildRequires:  cmake
BuildRequires:  git
BuildRequires:  libasan-static
BuildRequires:  libubsan-static
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(libpulse)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-app-1.0)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)


%description
Remote camera server


%prep
%setup -q -n %{name}-%{version}

# >> setup
# << setup

%build
# >> build pre
# << build pre
%cmake . 
make %{?_smp_mflags}

# >> build post
# << build post

%install
rm -rf %{buildroot}
# >> install pre
# << install pre
%make_install

# >> install post
# << install post

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
   %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%attr(2755, root, privileged) %{_bindir}/%{name}
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
# >> files
# << files
