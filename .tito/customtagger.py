import re
import os.path

from tito.common import run_command
from tito.tagger import VersionTagger


class CustomVersionTagger(VersionTagger):

    def _update_configure_ac(self, new_version):
        configure_ac = os.path.join(self.full_project_dir, 'configure.ac')
        with open(configure_ac, 'r') as f:
            content = re.sub(r'(AC_INIT.*?,\s+)\[.*?\]',
                             r'\1[%s]' % new_version.split('-')[0],
                             f.read())
        with open(configure_ac, 'w') as f:
            f.write(content)
        run_command("git add %s" % configure_ac)

    def _update_package_metadata(self, new_version):
        self._update_configure_ac(new_version)
        super()._update_package_metadata(new_version)
