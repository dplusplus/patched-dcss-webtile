# -*- coding: utf-8 -*-
import tornado.ioloop
import tornado.web

from util import DynamicTemplateLoader
from morgue_handler import MorgueHandler, DumpHandler

class MainHandler(tornado.web.RequestHandler):
    def write_error(self, status_code, **kwargs):
        if status_code in [403, 404, 503]:
            self.render('{0}.html'.format(status_code),
                message='現在メンテナンス中です。'
            )
        else:
            super(MainHandler, self).write_error(status_code, **kwargs)

    def get(self):
        raise tornado.web.HTTPError(503)


settings = {
    "static_path": "./webserver/static/",
    "template_loader": DynamicTemplateLoader.get("./webserver/templates/"),
    "gzip": True,
}

application = tornado.web.Application([
    (r"/morgue(-[0-9.]+)?/(.*)/(.+)", DumpHandler),
    (r"/morgue/(.*)/", MorgueHandler),
    (r"/.*", MainHandler),
], **settings)

if __name__ == "__main__":
    application.listen(port=8080)
    tornado.ioloop.IOLoop.instance().start()
