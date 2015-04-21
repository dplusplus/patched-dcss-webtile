# -*- coding: utf-8 -*-
import tornado.ioloop
import tornado.web

from util import DynamicTemplateLoader
import morgue_handler
import metafile_handler
from score_handler import ScoreTopNHandler

template503 = """\
<!DOCTYPE html>
<html lang="ja">
    <head>
        <title>Under maintenance</title>
        <meta charset="UTF-8">
        <link rel="stylesheet" type="text/css" href="/static/style.css">
        <link rel="icon" href="/static/stone_soup_icon-32x32.png" type="image/png">
    </head>
    <body style="font-family: none">
        <h2>503 Service Temporarily Unavailable</h2>
        <h3>現在メンテナンス中です。</h3>
        <div style="margin-top: 100px; text-align: center">
          <a class="twitter-timeline"  href="https://twitter.com/search?q=%23%E7%9F%B3%E9%8D%8B%E8%A9%A6%E9%A8%93%E9%AF%96"  data-widget-id="465028961756381185" data-chrome="nofooter">#石鍋試験鯖 に関するツイート</a>
          <script>!function(d,s,id){var js,fjs=d.getElementsByTagName(s)[0],p=/^http:/.test(d.location)?'http':'https';if(!d.getElementById(id)){js=d.createElement(s);js.id=id;js.src=p+"://platform.twitter.com/widgets.js";fjs.parentNode.insertBefore(js,fjs);}}(document,"script","twitter-wjs");</script>
        </div>
    </body>
</html>
"""


class MainHandler(tornado.web.RequestHandler):
    def write_error(self, status_code, **kwargs):
        if status_code == 503:
            self.finish(template503)
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
