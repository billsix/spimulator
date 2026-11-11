# 400 jumps, each in the delay slot of the one before, under -delayed_branches.
# Each delay slot nests one recursive run_spim call (C nests about 10,000 deep);
# the Python port hit its default recursion limit at about 330.
	.text
	.globl main
main:
	j d1
d1:
	j d2
d2:
	j d3
d3:
	j d4
d4:
	j d5
d5:
	j d6
d6:
	j d7
d7:
	j d8
d8:
	j d9
d9:
	j d10
d10:
	j d11
d11:
	j d12
d12:
	j d13
d13:
	j d14
d14:
	j d15
d15:
	j d16
d16:
	j d17
d17:
	j d18
d18:
	j d19
d19:
	j d20
d20:
	j d21
d21:
	j d22
d22:
	j d23
d23:
	j d24
d24:
	j d25
d25:
	j d26
d26:
	j d27
d27:
	j d28
d28:
	j d29
d29:
	j d30
d30:
	j d31
d31:
	j d32
d32:
	j d33
d33:
	j d34
d34:
	j d35
d35:
	j d36
d36:
	j d37
d37:
	j d38
d38:
	j d39
d39:
	j d40
d40:
	j d41
d41:
	j d42
d42:
	j d43
d43:
	j d44
d44:
	j d45
d45:
	j d46
d46:
	j d47
d47:
	j d48
d48:
	j d49
d49:
	j d50
d50:
	j d51
d51:
	j d52
d52:
	j d53
d53:
	j d54
d54:
	j d55
d55:
	j d56
d56:
	j d57
d57:
	j d58
d58:
	j d59
d59:
	j d60
d60:
	j d61
d61:
	j d62
d62:
	j d63
d63:
	j d64
d64:
	j d65
d65:
	j d66
d66:
	j d67
d67:
	j d68
d68:
	j d69
d69:
	j d70
d70:
	j d71
d71:
	j d72
d72:
	j d73
d73:
	j d74
d74:
	j d75
d75:
	j d76
d76:
	j d77
d77:
	j d78
d78:
	j d79
d79:
	j d80
d80:
	j d81
d81:
	j d82
d82:
	j d83
d83:
	j d84
d84:
	j d85
d85:
	j d86
d86:
	j d87
d87:
	j d88
d88:
	j d89
d89:
	j d90
d90:
	j d91
d91:
	j d92
d92:
	j d93
d93:
	j d94
d94:
	j d95
d95:
	j d96
d96:
	j d97
d97:
	j d98
d98:
	j d99
d99:
	j d100
d100:
	j d101
d101:
	j d102
d102:
	j d103
d103:
	j d104
d104:
	j d105
d105:
	j d106
d106:
	j d107
d107:
	j d108
d108:
	j d109
d109:
	j d110
d110:
	j d111
d111:
	j d112
d112:
	j d113
d113:
	j d114
d114:
	j d115
d115:
	j d116
d116:
	j d117
d117:
	j d118
d118:
	j d119
d119:
	j d120
d120:
	j d121
d121:
	j d122
d122:
	j d123
d123:
	j d124
d124:
	j d125
d125:
	j d126
d126:
	j d127
d127:
	j d128
d128:
	j d129
d129:
	j d130
d130:
	j d131
d131:
	j d132
d132:
	j d133
d133:
	j d134
d134:
	j d135
d135:
	j d136
d136:
	j d137
d137:
	j d138
d138:
	j d139
d139:
	j d140
d140:
	j d141
d141:
	j d142
d142:
	j d143
d143:
	j d144
d144:
	j d145
d145:
	j d146
d146:
	j d147
d147:
	j d148
d148:
	j d149
d149:
	j d150
d150:
	j d151
d151:
	j d152
d152:
	j d153
d153:
	j d154
d154:
	j d155
d155:
	j d156
d156:
	j d157
d157:
	j d158
d158:
	j d159
d159:
	j d160
d160:
	j d161
d161:
	j d162
d162:
	j d163
d163:
	j d164
d164:
	j d165
d165:
	j d166
d166:
	j d167
d167:
	j d168
d168:
	j d169
d169:
	j d170
d170:
	j d171
d171:
	j d172
d172:
	j d173
d173:
	j d174
d174:
	j d175
d175:
	j d176
d176:
	j d177
d177:
	j d178
d178:
	j d179
d179:
	j d180
d180:
	j d181
d181:
	j d182
d182:
	j d183
d183:
	j d184
d184:
	j d185
d185:
	j d186
d186:
	j d187
d187:
	j d188
d188:
	j d189
d189:
	j d190
d190:
	j d191
d191:
	j d192
d192:
	j d193
d193:
	j d194
d194:
	j d195
d195:
	j d196
d196:
	j d197
d197:
	j d198
d198:
	j d199
d199:
	j d200
d200:
	j d201
d201:
	j d202
d202:
	j d203
d203:
	j d204
d204:
	j d205
d205:
	j d206
d206:
	j d207
d207:
	j d208
d208:
	j d209
d209:
	j d210
d210:
	j d211
d211:
	j d212
d212:
	j d213
d213:
	j d214
d214:
	j d215
d215:
	j d216
d216:
	j d217
d217:
	j d218
d218:
	j d219
d219:
	j d220
d220:
	j d221
d221:
	j d222
d222:
	j d223
d223:
	j d224
d224:
	j d225
d225:
	j d226
d226:
	j d227
d227:
	j d228
d228:
	j d229
d229:
	j d230
d230:
	j d231
d231:
	j d232
d232:
	j d233
d233:
	j d234
d234:
	j d235
d235:
	j d236
d236:
	j d237
d237:
	j d238
d238:
	j d239
d239:
	j d240
d240:
	j d241
d241:
	j d242
d242:
	j d243
d243:
	j d244
d244:
	j d245
d245:
	j d246
d246:
	j d247
d247:
	j d248
d248:
	j d249
d249:
	j d250
d250:
	j d251
d251:
	j d252
d252:
	j d253
d253:
	j d254
d254:
	j d255
d255:
	j d256
d256:
	j d257
d257:
	j d258
d258:
	j d259
d259:
	j d260
d260:
	j d261
d261:
	j d262
d262:
	j d263
d263:
	j d264
d264:
	j d265
d265:
	j d266
d266:
	j d267
d267:
	j d268
d268:
	j d269
d269:
	j d270
d270:
	j d271
d271:
	j d272
d272:
	j d273
d273:
	j d274
d274:
	j d275
d275:
	j d276
d276:
	j d277
d277:
	j d278
d278:
	j d279
d279:
	j d280
d280:
	j d281
d281:
	j d282
d282:
	j d283
d283:
	j d284
d284:
	j d285
d285:
	j d286
d286:
	j d287
d287:
	j d288
d288:
	j d289
d289:
	j d290
d290:
	j d291
d291:
	j d292
d292:
	j d293
d293:
	j d294
d294:
	j d295
d295:
	j d296
d296:
	j d297
d297:
	j d298
d298:
	j d299
d299:
	j d300
d300:
	j d301
d301:
	j d302
d302:
	j d303
d303:
	j d304
d304:
	j d305
d305:
	j d306
d306:
	j d307
d307:
	j d308
d308:
	j d309
d309:
	j d310
d310:
	j d311
d311:
	j d312
d312:
	j d313
d313:
	j d314
d314:
	j d315
d315:
	j d316
d316:
	j d317
d317:
	j d318
d318:
	j d319
d319:
	j d320
d320:
	j d321
d321:
	j d322
d322:
	j d323
d323:
	j d324
d324:
	j d325
d325:
	j d326
d326:
	j d327
d327:
	j d328
d328:
	j d329
d329:
	j d330
d330:
	j d331
d331:
	j d332
d332:
	j d333
d333:
	j d334
d334:
	j d335
d335:
	j d336
d336:
	j d337
d337:
	j d338
d338:
	j d339
d339:
	j d340
d340:
	j d341
d341:
	j d342
d342:
	j d343
d343:
	j d344
d344:
	j d345
d345:
	j d346
d346:
	j d347
d347:
	j d348
d348:
	j d349
d349:
	j d350
d350:
	j d351
d351:
	j d352
d352:
	j d353
d353:
	j d354
d354:
	j d355
d355:
	j d356
d356:
	j d357
d357:
	j d358
d358:
	j d359
d359:
	j d360
d360:
	j d361
d361:
	j d362
d362:
	j d363
d363:
	j d364
d364:
	j d365
d365:
	j d366
d366:
	j d367
d367:
	j d368
d368:
	j d369
d369:
	j d370
d370:
	j d371
d371:
	j d372
d372:
	j d373
d373:
	j d374
d374:
	j d375
d375:
	j d376
d376:
	j d377
d377:
	j d378
d378:
	j d379
d379:
	j d380
d380:
	j d381
d381:
	j d382
d382:
	j d383
d383:
	j d384
d384:
	j d385
d385:
	j d386
d386:
	j d387
d387:
	j d388
d388:
	j d389
d389:
	j d390
d390:
	j d391
d391:
	j d392
d392:
	j d393
d393:
	j d394
d394:
	j d395
d395:
	j d396
d396:
	j d397
d397:
	j d398
d398:
	j d399
d399:
	j d400
d400:
	li $v0, 10
	syscall
