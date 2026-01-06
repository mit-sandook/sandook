from .context import get_logger
from zope import interface

logger = get_logger()


class App(interface.Interface):
    exp = interface.Attribute("Experiment instance")
    client = interface.Attribute("Client instance")

    def run():
        pass
