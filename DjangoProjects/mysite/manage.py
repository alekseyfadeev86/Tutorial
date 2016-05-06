#!/usr/bin/env python
# Необходимы django 1.8 и python 3.3
# sudo apt-get install python3-module-django-dbbackend-sqlite3
# http://djbook.ru/rel1.8/intro/tutorial02.html

import os
import sys

if __name__ == "__main__":
    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "mysite.settings")
    from django.core.management import execute_from_command_line

    execute_from_command_line(sys.argv)
