# -*- coding: utf-8 -*-
import tornado.ioloop
import tornado.web

from util import DynamicTemplateLoader
import morgue_handler
import metafile_handler
from score_handler import ScoreTopNHandler

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
    (r"/morgue(-[^/]+)?/(.*)/(.+\.)(txt|map|lst)", morgue_handler.DumpHandler),
    (r"/morgue/([^/]+)/", morgue_handler.MorgueHandler),
    (r"/morgue/", morgue_handler.MorgueIndexHandler),
    (r"/scoring/top-(\d+).html", ScoreTopNHandler),
    (r"/meta/(([^/]+)/)?", metafile_handler.MetaFileIndexHandler),
    (r"/meta/([^/]+)/([^/]+)", metafile_handler.MetaFileHandler),
    (r"/rcfiles/(([^/]+)/)?", metafile_handler.RCFileIndexHandler),
    (r"/rcfiles/([^/]+)/([^/]+\.rc)", metafile_handler.RCFileHandler),
    (r"/.*", MainHandler),
], **settings)

if __name__ == "__main__":
    application.listen(port=8080)
    tornado.ioloop.IOLoop.instance().start()
