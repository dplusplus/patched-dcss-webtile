# -*- coding: utf-8 -*-
import tornado.web

import os.path
import re

class MorgueHandler(tornado.web.RequestHandler):
    def get(self, player_name):
        player_name = player_name.rstrip('/')

        if not os.path.exists('./rcs/{0}.macro'.format(player_name)):
            raise tornado.web.HTTPError(404)
        else:
            exists_manual_dump =  os.path.exists('./rcs/{0}/{0}.txt'.format(player_name))

            dump_paths = [dump for dump in os.listdir('./rcs/'+player_name) if re.match("morgue.*txt", dump)]
            dump_paths.sort(reverse=True)

            self.set_header('Content-Type', 'text/html; charset="utf-8"')
            self.render('morgue.html',
                        exists_manual_dump=exists_manual_dump,
                        player_name=player_name,
                        dump_paths=dump_paths)


class DumpHandler(tornado.web.RequestHandler):
    def get(self, player_name, dump_id):
        dump_path = '{0}/{1}'.format(player_name, dump_id)

        if not os.path.exists('./rcs/' + dump_path):
            raise tornado.web.HTTPError(404)
        elif not re.match(
            r'({0}|(morgue|crash)-(recursive-)?{0}-\d{{8}}-\d{{6}}).(lst|map|txt|where)'.format(player_name),
            dump_id):
            raise tornado.web.HTTPError(403)
        else:
            self.set_header('Content-Type', 'text/plain; charset="utf-8"')
            self.render('../../rcs/' + dump_path)
