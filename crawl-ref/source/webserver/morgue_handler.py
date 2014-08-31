# -*- coding: utf-8 -*-
import tornado.web

import os.path
import re


class RequestHandler(tornado.web.RequestHandler):
    def write_error(self, status_code, **kwargs):
        if status_code in [403, 404, 503]:
            self.render('{0}.html'.format(status_code),
                message=kwargs['exc_info'][1].log_message if 'exc_info' in kwargs else None
            )
        else:
            super(RequestHandler, self).write_error(status_code, **kwargs)


class MorgueHandler(RequestHandler):
    def get(self, player_name):
        def exists_user(player_name): return os.path.exists('./rcs/{0}.macro'.format(player_name))

        if not exists_user(player_name):
            raise tornado.web.HTTPError(404)
        else:
            # for dcss trunk
            dcss_trunk = './rcs/{0}/'.format(player_name)
            exists_manual_dump = os.path.exists(dcss_trunk + '{0}.txt'.format(player_name))
            dump_paths = sorted([f for f in os.listdir(dcss_trunk) if re.match("morgue.*txt", f)], reverse=True)

            # for dcss 0.14-1
            dcss_0_14 = '../../dcss-0.14/source/rcs/{0}/'.format(player_name)
            exists_manual_dump_0_14 = os.path.exists(dcss_0_14 + '{0}.txt'.format(player_name))
            dump_paths_0_14 = sorted([f for f in os.listdir(dcss_0_14) if re.match("morgue.*txt", f)], reverse=True)

            self.set_header('Content-Type', 'text/html; charset="utf-8"')
            self.render('morgue.html',
                        player_name=player_name,
                        exists_manual_dump=exists_manual_dump,
                        exists_manual_dump_0_14=exists_manual_dump_0_14,
                        dump_paths=dump_paths,
                        dump_paths_0_14=dump_paths_0_14)


class DumpHandler(RequestHandler):
    def get(self, morgue_ver, player_name, dump_id, suffix):
        dump_path = '{0}/{1}{2}'.format(player_name, dump_id, suffix)
        morgue_dir = '../../dcss{0}/source/rcs/'.format(morgue_ver) if morgue_ver else './rcs/'

        if not os.path.exists(morgue_dir + dump_path):
            raise tornado.web.HTTPError(404)
        elif not re.match(
                     r'({0}|(morgue|crash)-(recursive-)?{0}-\d{{8}}-\d{{6}}).(lst|map|txt|where)'.format(player_name),
                     dump_id + suffix
                 ):
            raise tornado.web.HTTPError(403)
        else:
            path = '{0}{1}/{2}'.format(morgue_dir, player_name, dump_id)
            if dump_id.startswith('crash'):
                self.set_header('Content-Type', 'text/plain; charset="utf-8"')
                self.write(open(path + 'txt').read())
                return

            params = {
                'dump': open(path + 'txt').read(),
                'map': open(path + 'map').read(),
                'lst': open(path + 'lst').read(),
                'suffix': suffix,
            }

            self.set_header('Content-Type', 'text/html; charset="utf-8"')
            self.render('dump.html', **params)
