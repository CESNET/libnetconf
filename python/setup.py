from distutils.core import setup, Extension

netconfModule = Extension("netconf",
                           sources=["netconf.c", "session.c"],
                           depends=["netconf.h"],
                           libraries=["netconf"],
                           extra_compile_args=["-Wall"],
                        )

setup(name='netconf',
      version='0.8.0',
      author='Radek Krejci',
      author_email='rkrejci@cesnet.cz',
      description='libnetconf Python bindings.',
      long_description = 'TBD',
      url='https://libnetconf.googlecode.com',
      ext_modules=[netconfModule],
      platforms=['Linux'],
      license='BSD License',
      )
