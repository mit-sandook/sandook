import os
from .log import get_logger


logger = get_logger()


def remove_file(file_path):
    """
    Delete a file. If it does not exist, silently move on.

    file_path: Path to the file being deleted.
    """
    try:
        os.remove(file_path)
    except OSError:
        pass
