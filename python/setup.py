from distutils.core import setup, Extension

setup(name='netconf',
      version='0.8.0',
      author='Radek Krejci',
      author_email='rkrejci@cesnet.cz',
      description='libnetconf Python bindings.',
      url='https://libnetconf.googlecode.com',
      ext_modules=[Extension("netconf", ["netconf.c", "session.c"], libraries=["netconf"], extra_compile_args=["-g"])],
      platforms=['Linux'],
      license='BSD License',
      )
