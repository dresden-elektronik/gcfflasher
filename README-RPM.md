# Creating the RPM packages
Tested with Fedora-40 and RHEL(or it's clones)-9
## Requirements
- mock
- rpmbuild
## Build
**Without unpacking the archive!!!**
1. rpmbuild -ts gcfflasher-<version>.tar.gz
2. mock -r <change root> <path to generated SRPM file>
