LOAD '/home/blizhan/repo/github/duckomo/build/release/extension/duckomo/duckomo.duckdb_extension'; SET threads=1; SET preserve_insertion_order=true;
COPY (SELECT om_source.logical_index AS logical_index, om_source.point_index AS point_index, om_source.parent_point_index AS parent_point_index, om_source.axis_indices[1] AS axis0, om_source.axis_indices[2] AS axis1, lat, lon, value FROM read_om('/home/blizhan/repo/github/duckomo/build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om', grid := {
    'version': 1,
    'type': 'reduced_gaussian',
    'numeric_policy': 'openmeteo_f32_v1',
    'earth': {
        'model': 'wgs84',
        'semi_major_m': 6378137.0,
        'inverse_flattening': 298.257223563
    },
    'layout': {
        'order': 'row_major'
    },
    'parameters': {
        'n': 1280,
        'latitude_rule': 'openmeteo_approx_v1',
        'rows': [
            {
                'latitude': 89.94727325439453,
                'point_count': 20,
                'longitude_origin': 0.0,
                'longitude_step': 18.0
            },
            {
                'latitude': 89.8769760131836,
                'point_count': 24,
                'longitude_origin': 0.0,
                'longitude_step': 15.0
            },
            {
                'latitude': 89.80667877197266,
                'point_count': 28,
                'longitude_origin': 0.0,
                'longitude_step': 12.857142448425293
            },
            {
                'latitude': 89.73637390136719,
                'point_count': 32,
                'longitude_origin': 0.0,
                'longitude_step': 11.25
            },
            {
                'latitude': 89.66607666015625,
                'point_count': 36,
                'longitude_origin': 0.0,
                'longitude_step': 10.0
            },
            {
                'latitude': 89.59577941894531,
                'point_count': 40,
                'longitude_origin': 0.0,
                'longitude_step': 9.0
            },
            {
                'latitude': 89.52548217773438,
                'point_count': 44,
                'longitude_origin': 0.0,
                'longitude_step': 8.181818008422852
            },
            {
                'latitude': 89.45518493652344,
                'point_count': 48,
                'longitude_origin': 0.0,
                'longitude_step': 7.5
            },
            {
                'latitude': 89.38488006591797,
                'point_count': 52,
                'longitude_origin': 0.0,
                'longitude_step': 6.92307710647583
            },
            {
                'latitude': 89.31458282470703,
                'point_count': 56,
                'longitude_origin': 0.0,
                'longitude_step': 6.4285712242126465
            },
            {
                'latitude': 89.2442855834961,
                'point_count': 60,
                'longitude_origin': 0.0,
                'longitude_step': 6.0
            },
            {
                'latitude': 89.17398834228516,
                'point_count': 64,
                'longitude_origin': 0.0,
                'longitude_step': 5.625
            },
            {
                'latitude': 89.10369110107422,
                'point_count': 68,
                'longitude_origin': 0.0,
                'longitude_step': 5.294117450714111
            },
            {
                'latitude': 89.03338623046875,
                'point_count': 72,
                'longitude_origin': 0.0,
                'longitude_step': 5.0
            },
            {
                'latitude': 88.96308898925781,
                'point_count': 76,
                'longitude_origin': 0.0,
                'longitude_step': 4.736842155456543
            },
            {
                'latitude': 88.89279174804688,
                'point_count': 80,
                'longitude_origin': 0.0,
                'longitude_step': 4.5
            },
            {
                'latitude': 88.82249450683594,
                'point_count': 84,
                'longitude_origin': 0.0,
                'longitude_step': 4.285714149475098
            },
            {
                'latitude': 88.752197265625,
                'point_count': 88,
                'longitude_origin': 0.0,
                'longitude_step': 4.090909004211426
            },
            {
                'latitude': 88.68189239501953,
                'point_count': 92,
                'longitude_origin': 0.0,
                'longitude_step': 3.91304349899292
            },
            {
                'latitude': 88.6115951538086,
                'point_count': 96,
                'longitude_origin': 0.0,
                'longitude_step': 3.75
            },
            {
                'latitude': 88.54129791259766,
                'point_count': 100,
                'longitude_origin': 0.0,
                'longitude_step': 3.5999999046325684
            },
            {
                'latitude': 88.47100067138672,
                'point_count': 104,
                'longitude_origin': 0.0,
                'longitude_step': 3.461538553237915
            },
            {
                'latitude': 88.40070343017578,
                'point_count': 108,
                'longitude_origin': 0.0,
                'longitude_step': 3.3333332538604736
            },
            {
                'latitude': 88.33039855957031,
                'point_count': 112,
                'longitude_origin': 0.0,
                'longitude_step': 3.2142856121063232
            },
            {
                'latitude': 88.26010131835938,
                'point_count': 116,
                'longitude_origin': 0.0,
                'longitude_step': 3.1034483909606934
            },
            {
                'latitude': 88.18980407714844,
                'point_count': 120,
                'longitude_origin': 0.0,
                'longitude_step': 3.0
            },
            {
                'latitude': 88.1195068359375,
                'point_count': 124,
                'longitude_origin': 0.0,
                'longitude_step': 2.903225898742676
            },
            {
                'latitude': 88.04920959472656,
                'point_count': 128,
                'longitude_origin': 0.0,
                'longitude_step': 2.8125
            },
            {
                'latitude': 87.9789047241211,
                'point_count': 132,
                'longitude_origin': 0.0,
                'longitude_step': 2.7272727489471436
            },
            {
                'latitude': 87.90860748291016,
                'point_count': 136,
                'longitude_origin': 0.0,
                'longitude_step': 2.6470587253570557
            },
            {
                'latitude': 87.83831024169922,
                'point_count': 140,
                'longitude_origin': 0.0,
                'longitude_step': 2.5714285373687744
            },
            {
                'latitude': 87.76801300048828,
                'point_count': 144,
                'longitude_origin': 0.0,
                'longitude_step': 2.5
            },
            {
                'latitude': 87.69771575927734,
                'point_count': 148,
                'longitude_origin': 0.0,
                'longitude_step': 2.4324324131011963
            },
            {
                'latitude': 87.62741088867188,
                'point_count': 152,
                'longitude_origin': 0.0,
                'longitude_step': 2.3684210777282715
            },
            {
                'latitude': 87.55711364746094,
                'point_count': 156,
                'longitude_origin': 0.0,
                'longitude_step': 2.307692289352417
            },
            {
                'latitude': 87.48681640625,
                'point_count': 160,
                'longitude_origin': 0.0,
                'longitude_step': 2.25
            },
            {
                'latitude': 87.41651916503906,
                'point_count': 164,
                'longitude_origin': 0.0,
                'longitude_step': 2.195122003555298
            },
            {
                'latitude': 87.34622192382812,
                'point_count': 168,
                'longitude_origin': 0.0,
                'longitude_step': 2.142857074737549
            },
            {
                'latitude': 87.27591705322266,
                'point_count': 172,
                'longitude_origin': 0.0,
                'longitude_step': 2.0930233001708984
            },
            {
                'latitude': 87.20561981201172,
                'point_count': 176,
                'longitude_origin': 0.0,
                'longitude_step': 2.045454502105713
            },
            {
                'latitude': 87.13532257080078,
                'point_count': 180,
                'longitude_origin': 0.0,
                'longitude_step': 2.0
            },
            {
                'latitude': 87.06502532958984,
                'point_count': 184,
                'longitude_origin': 0.0,
                'longitude_step': 1.95652174949646
            },
            {
                'latitude': 86.9947280883789,
                'point_count': 188,
                'longitude_origin': 0.0,
                'longitude_step': 1.914893627166748
            },
            {
                'latitude': 86.92442321777344,
                'point_count': 192,
                'longitude_origin': 0.0,
                'longitude_step': 1.875
            },
            {
                'latitude': 86.8541259765625,
                'point_count': 196,
                'longitude_origin': 0.0,
                'longitude_step': 1.836734652519226
            },
            {
                'latitude': 86.78382873535156,
                'point_count': 200,
                'longitude_origin': 0.0,
                'longitude_step': 1.7999999523162842
            },
            {
                'latitude': 86.71353149414062,
                'point_count': 204,
                'longitude_origin': 0.0,
                'longitude_step': 1.7647058963775635
            },
            {
                'latitude': 86.64323425292969,
                'point_count': 208,
                'longitude_origin': 0.0,
                'longitude_step': 1.7307692766189575
            },
            {
                'latitude': 86.57292938232422,
                'point_count': 212,
                'longitude_origin': 0.0,
                'longitude_step': 1.698113203048706
            },
            {
                'latitude': 86.50263214111328,
                'point_count': 216,
                'longitude_origin': 0.0,
                'longitude_step': 1.6666666269302368
            },
            {
                'latitude': 86.43233489990234,
                'point_count': 220,
                'longitude_origin': 0.0,
                'longitude_step': 1.6363636255264282
            },
            {
                'latitude': 86.3620376586914,
                'point_count': 224,
                'longitude_origin': 0.0,
                'longitude_step': 1.6071428060531616
            },
            {
                'latitude': 86.29174041748047,
                'point_count': 228,
                'longitude_origin': 0.0,
                'longitude_step': 1.5789474248886108
            },
            {
                'latitude': 86.221435546875,
                'point_count': 232,
                'longitude_origin': 0.0,
                'longitude_step': 1.5517241954803467
            },
            {
                'latitude': 86.15113830566406,
                'point_count': 236,
                'longitude_origin': 0.0,
                'longitude_step': 1.5254237651824951
            },
            {
                'latitude': 86.08084106445312,
                'point_count': 240,
                'longitude_origin': 0.0,
                'longitude_step': 1.5
            },
            {
                'latitude': 86.01054382324219,
                'point_count': 244,
                'longitude_origin': 0.0,
                'longitude_step': 1.4754098653793335
            },
            {
                'latitude': 85.94024658203125,
                'point_count': 248,
                'longitude_origin': 0.0,
                'longitude_step': 1.451612949371338
            },
            {
                'latitude': 85.86994171142578,
                'point_count': 252,
                'longitude_origin': 0.0,
                'longitude_step': 1.4285714626312256
            },
            {
                'latitude': 85.79964447021484,
                'point_count': 256,
                'longitude_origin': 0.0,
                'longitude_step': 1.40625
            },
            {
                'latitude': 85.7293472290039,
                'point_count': 260,
                'longitude_origin': 0.0,
                'longitude_step': 1.384615421295166
            },
            {
                'latitude': 85.65904998779297,
                'point_count': 264,
                'longitude_origin': 0.0,
                'longitude_step': 1.3636363744735718
            },
            {
                'latitude': 85.58875274658203,
                'point_count': 268,
                'longitude_origin': 0.0,
                'longitude_step': 1.3432835340499878
            },
            {
                'latitude': 85.51844787597656,
                'point_count': 272,
                'longitude_origin': 0.0,
                'longitude_step': 1.3235293626785278
            },
            {
                'latitude': 85.44815063476562,
                'point_count': 276,
                'longitude_origin': 0.0,
                'longitude_step': 1.3043478727340698
            },
            {
                'latitude': 85.37785339355469,
                'point_count': 280,
                'longitude_origin': 0.0,
                'longitude_step': 1.2857142686843872
            },
            {
                'latitude': 85.30755615234375,
                'point_count': 284,
                'longitude_origin': 0.0,
                'longitude_step': 1.2676056623458862
            },
            {
                'latitude': 85.23725891113281,
                'point_count': 288,
                'longitude_origin': 0.0,
                'longitude_step': 1.25
            },
            {
                'latitude': 85.16695404052734,
                'point_count': 292,
                'longitude_origin': 0.0,
                'longitude_step': 1.2328766584396362
            },
            {
                'latitude': 85.0966567993164,
                'point_count': 296,
                'longitude_origin': 0.0,
                'longitude_step': 1.2162162065505981
            },
            {
                'latitude': 85.02635955810547,
                'point_count': 300,
                'longitude_origin': 0.0,
                'longitude_step': 1.2000000476837158
            },
            {
                'latitude': 84.95606231689453,
                'point_count': 304,
                'longitude_origin': 0.0,
                'longitude_step': 1.1842105388641357
            },
            {
                'latitude': 84.8857650756836,
                'point_count': 308,
                'longitude_origin': 0.0,
                'longitude_step': 1.1688311100006104
            },
            {
                'latitude': 84.81546020507812,
                'point_count': 312,
                'longitude_origin': 0.0,
                'longitude_step': 1.1538461446762085
            },
            {
                'latitude': 84.74516296386719,
                'point_count': 316,
                'longitude_origin': 0.0,
                'longitude_step': 1.1392405033111572
            },
            {
                'latitude': 84.67486572265625,
                'point_count': 320,
                'longitude_origin': 0.0,
                'longitude_step': 1.125
            },
            {
                'latitude': 84.60456848144531,
                'point_count': 324,
                'longitude_origin': 0.0,
                'longitude_step': 1.1111111640930176
            },
            {
                'latitude': 84.53427124023438,
                'point_count': 328,
                'longitude_origin': 0.0,
                'longitude_step': 1.097561001777649
            },
            {
                'latitude': 84.4639663696289,
                'point_count': 332,
                'longitude_origin': 0.0,
                'longitude_step': 1.0843373537063599
            },
            {
                'latitude': 84.39366912841797,
                'point_count': 336,
                'longitude_origin': 0.0,
                'longitude_step': 1.0714285373687744
            },
            {
                'latitude': 84.32337188720703,
                'point_count': 340,
                'longitude_origin': 0.0,
                'longitude_step': 1.058823585510254
            },
            {
                'latitude': 84.2530746459961,
                'point_count': 344,
                'longitude_origin': 0.0,
                'longitude_step': 1.0465116500854492
            },
            {
                'latitude': 84.18277740478516,
                'point_count': 348,
                'longitude_origin': 0.0,
                'longitude_step': 1.034482717514038
            },
            {
                'latitude': 84.11247253417969,
                'point_count': 352,
                'longitude_origin': 0.0,
                'longitude_step': 1.0227272510528564
            },
            {
                'latitude': 84.04217529296875,
                'point_count': 356,
                'longitude_origin': 0.0,
                'longitude_step': 1.0112359523773193
            },
            {
                'latitude': 83.97187805175781,
                'point_count': 360,
                'longitude_origin': 0.0,
                'longitude_step': 1.0
            },
            {
                'latitude': 83.90158081054688,
                'point_count': 364,
                'longitude_origin': 0.0,
                'longitude_step': 0.9890109896659851
            },
            {
                'latitude': 83.83128356933594,
                'point_count': 368,
                'longitude_origin': 0.0,
                'longitude_step': 0.97826087474823
            },
            {
                'latitude': 83.76097869873047,
                'point_count': 372,
                'longitude_origin': 0.0,
                'longitude_step': 0.9677419066429138
            },
            {
                'latitude': 83.69068145751953,
                'point_count': 376,
                'longitude_origin': 0.0,
                'longitude_step': 0.957446813583374
            },
            {
                'latitude': 83.6203842163086,
                'point_count': 380,
                'longitude_origin': 0.0,
                'longitude_step': 0.9473684430122375
            },
            {
                'latitude': 83.55008697509766,
                'point_count': 384,
                'longitude_origin': 0.0,
                'longitude_step': 0.9375
            },
            {
                'latitude': 83.47978973388672,
                'point_count': 388,
                'longitude_origin': 0.0,
                'longitude_step': 0.9278350472450256
            },
            {
                'latitude': 83.40948486328125,
                'point_count': 392,
                'longitude_origin': 0.0,
                'longitude_step': 0.918367326259613
            },
            {
                'latitude': 83.33918762207031,
                'point_count': 396,
                'longitude_origin': 0.0,
                'longitude_step': 0.9090909361839294
            },
            {
                'latitude': 83.26889038085938,
                'point_count': 400,
                'longitude_origin': 0.0,
                'longitude_step': 0.8999999761581421
            },
            {
                'latitude': 83.19859313964844,
                'point_count': 404,
                'longitude_origin': 0.0,
                'longitude_step': 0.8910890817642212
            },
            {
                'latitude': 83.1282958984375,
                'point_count': 408,
                'longitude_origin': 0.0,
                'longitude_step': 0.8823529481887817
            },
            {
                'latitude': 83.05799102783203,
                'point_count': 412,
                'longitude_origin': 0.0,
                'longitude_step': 0.8737863898277283
            },
            {
                'latitude': 82.9876937866211,
                'point_count': 416,
                'longitude_origin': 0.0,
                'longitude_step': 0.8653846383094788
            },
            {
                'latitude': 82.91739654541016,
                'point_count': 420,
                'longitude_origin': 0.0,
                'longitude_step': 0.8571428656578064
            },
            {
                'latitude': 82.84709930419922,
                'point_count': 424,
                'longitude_origin': 0.0,
                'longitude_step': 0.849056601524353
            },
            {
                'latitude': 82.77680206298828,
                'point_count': 428,
                'longitude_origin': 0.0,
                'longitude_step': 0.84112149477005
            },
            {
                'latitude': 82.70649719238281,
                'point_count': 432,
                'longitude_origin': 0.0,
                'longitude_step': 0.8333333134651184
            },
            {
                'latitude': 82.63619995117188,
                'point_count': 436,
                'longitude_origin': 0.0,
                'longitude_step': 0.8256880640983582
            },
            {
                'latitude': 82.56590270996094,
                'point_count': 440,
                'longitude_origin': 0.0,
                'longitude_step': 0.8181818127632141
            },
            {
                'latitude': 82.49560546875,
                'point_count': 444,
                'longitude_origin': 0.0,
                'longitude_step': 0.8108108043670654
            },
            {
                'latitude': 82.42530822753906,
                'point_count': 448,
                'longitude_origin': 0.0,
                'longitude_step': 0.8035714030265808
            },
            {
                'latitude': 82.3550033569336,
                'point_count': 452,
                'longitude_origin': 0.0,
                'longitude_step': 0.7964601516723633
            },
            {
                'latitude': 82.28470611572266,
                'point_count': 456,
                'longitude_origin': 0.0,
                'longitude_step': 0.7894737124443054
            },
            {
                'latitude': 82.21440887451172,
                'point_count': 460,
                'longitude_origin': 0.0,
                'longitude_step': 0.782608687877655
            },
            {
                'latitude': 82.14411163330078,
                'point_count': 464,
                'longitude_origin': 0.0,
                'longitude_step': 0.7758620977401733
            },
            {
                'latitude': 82.07381439208984,
                'point_count': 468,
                'longitude_origin': 0.0,
                'longitude_step': 0.7692307829856873
            },
            {
                'latitude': 82.00350952148438,
                'point_count': 472,
                'longitude_origin': 0.0,
                'longitude_step': 0.7627118825912476
            },
            {
                'latitude': 81.93321228027344,
                'point_count': 476,
                'longitude_origin': 0.0,
                'longitude_step': 0.756302535533905
            },
            {
                'latitude': 81.8629150390625,
                'point_count': 480,
                'longitude_origin': 0.0,
                'longitude_step': 0.75
            },
            {
                'latitude': 81.79261779785156,
                'point_count': 484,
                'longitude_origin': 0.0,
                'longitude_step': 0.7438016533851624
            },
            {
                'latitude': 81.72232055664062,
                'point_count': 488,
                'longitude_origin': 0.0,
                'longitude_step': 0.7377049326896667
            },
            {
                'latitude': 81.65201568603516,
                'point_count': 492,
                'longitude_origin': 0.0,
                'longitude_step': 0.7317073345184326
            },
            {
                'latitude': 81.58171844482422,
                'point_count': 496,
                'longitude_origin': 0.0,
                'longitude_step': 0.725806474685669
            },
            {
                'latitude': 81.51142120361328,
                'point_count': 500,
                'longitude_origin': 0.0,
                'longitude_step': 0.7200000286102295
            },
            {
                'latitude': 81.44112396240234,
                'point_count': 504,
                'longitude_origin': 0.0,
                'longitude_step': 0.7142857313156128
            },
            {
                'latitude': 81.3708267211914,
                'point_count': 508,
                'longitude_origin': 0.0,
                'longitude_step': 0.7086614370346069
            },
            {
                'latitude': 81.30052185058594,
                'point_count': 512,
                'longitude_origin': 0.0,
                'longitude_step': 0.703125
            },
            {
                'latitude': 81.230224609375,
                'point_count': 516,
                'longitude_origin': 0.0,
                'longitude_step': 0.6976743936538696
            },
            {
                'latitude': 81.15992736816406,
                'point_count': 520,
                'longitude_origin': 0.0,
                'longitude_step': 0.692307710647583
            },
            {
                'latitude': 81.08963012695312,
                'point_count': 524,
                'longitude_origin': 0.0,
                'longitude_step': 0.6870229244232178
            },
            {
                'latitude': 81.01933288574219,
                'point_count': 528,
                'longitude_origin': 0.0,
                'longitude_step': 0.6818181872367859
            },
            {
                'latitude': 80.94902801513672,
                'point_count': 532,
                'longitude_origin': 0.0,
                'longitude_step': 0.6766917109489441
            },
            {
                'latitude': 80.87873077392578,
                'point_count': 536,
                'longitude_origin': 0.0,
                'longitude_step': 0.6716417670249939
            },
            {
                'latitude': 80.80843353271484,
                'point_count': 540,
                'longitude_origin': 0.0,
                'longitude_step': 0.6666666865348816
            },
            {
                'latitude': 80.7381362915039,
                'point_count': 544,
                'longitude_origin': 0.0,
                'longitude_step': 0.6617646813392639
            },
            {
                'latitude': 80.66783905029297,
                'point_count': 548,
                'longitude_origin': 0.0,
                'longitude_step': 0.6569343209266663
            },
            {
                'latitude': 80.5975341796875,
                'point_count': 552,
                'longitude_origin': 0.0,
                'longitude_step': 0.6521739363670349
            },
            {
                'latitude': 80.52723693847656,
                'point_count': 556,
                'longitude_origin': 0.0,
                'longitude_step': 0.6474820375442505
            },
            {
                'latitude': 80.45693969726562,
                'point_count': 560,
                'longitude_origin': 0.0,
                'longitude_step': 0.6428571343421936
            },
            {
                'latitude': 80.38664245605469,
                'point_count': 564,
                'longitude_origin': 0.0,
                'longitude_step': 0.6382978558540344
            },
            {
                'latitude': 80.31634521484375,
                'point_count': 568,
                'longitude_origin': 0.0,
                'longitude_step': 0.6338028311729431
            },
            {
                'latitude': 80.24604034423828,
                'point_count': 572,
                'longitude_origin': 0.0,
                'longitude_step': 0.6293706297874451
            },
            {
                'latitude': 80.17574310302734,
                'point_count': 576,
                'longitude_origin': 0.0,
                'longitude_step': 0.625
            },
            {
                'latitude': 80.1054458618164,
                'point_count': 580,
                'longitude_origin': 0.0,
                'longitude_step': 0.6206896305084229
            },
            {
                'latitude': 80.03514862060547,
                'point_count': 584,
                'longitude_origin': 0.0,
                'longitude_step': 0.6164383292198181
            },
            {
                'latitude': 79.96485137939453,
                'point_count': 588,
                'longitude_origin': 0.0,
                'longitude_step': 0.6122449040412903
            },
            {
                'latitude': 79.89454650878906,
                'point_count': 592,
                'longitude_origin': 0.0,
                'longitude_step': 0.6081081032752991
            },
            {
                'latitude': 79.82424926757812,
                'point_count': 596,
                'longitude_origin': 0.0,
                'longitude_step': 0.6040268540382385
            },
            {
                'latitude': 79.75395202636719,
                'point_count': 600,
                'longitude_origin': 0.0,
                'longitude_step': 0.6000000238418579
            },
            {
                'latitude': 79.68365478515625,
                'point_count': 604,
                'longitude_origin': 0.0,
                'longitude_step': 0.5960264801979065
            },
            {
                'latitude': 79.61335754394531,
                'point_count': 608,
                'longitude_origin': 0.0,
                'longitude_step': 0.5921052694320679
            },
            {
                'latitude': 79.54305267333984,
                'point_count': 612,
                'longitude_origin': 0.0,
                'longitude_step': 0.5882353186607361
            },
            {
                'latitude': 79.4727554321289,
                'point_count': 616,
                'longitude_origin': 0.0,
                'longitude_step': 0.5844155550003052
            },
            {
                'latitude': 79.40245819091797,
                'point_count': 620,
                'longitude_origin': 0.0,
                'longitude_step': 0.5806451439857483
            },
            {
                'latitude': 79.33216094970703,
                'point_count': 624,
                'longitude_origin': 0.0,
                'longitude_step': 0.5769230723381042
            },
            {
                'latitude': 79.2618637084961,
                'point_count': 628,
                'longitude_origin': 0.0,
                'longitude_step': 0.5732483863830566
            },
            {
                'latitude': 79.19155883789062,
                'point_count': 632,
                'longitude_origin': 0.0,
                'longitude_step': 0.5696202516555786
            },
            {
                'latitude': 79.12126159667969,
                'point_count': 636,
                'longitude_origin': 0.0,
                'longitude_step': 0.5660377144813538
            },
            {
                'latitude': 79.05096435546875,
                'point_count': 640,
                'longitude_origin': 0.0,
                'longitude_step': 0.5625
            },
            {
                'latitude': 78.98066711425781,
                'point_count': 644,
                'longitude_origin': 0.0,
                'longitude_step': 0.5590062141418457
            },
            {
                'latitude': 78.91036987304688,
                'point_count': 648,
                'longitude_origin': 0.0,
                'longitude_step': 0.5555555820465088
            },
            {
                'latitude': 78.8400650024414,
                'point_count': 652,
                'longitude_origin': 0.0,
                'longitude_step': 0.5521472096443176
            },
            {
                'latitude': 78.76976776123047,
                'point_count': 656,
                'longitude_origin': 0.0,
                'longitude_step': 0.5487805008888245
            },
            {
                'latitude': 78.69947052001953,
                'point_count': 660,
                'longitude_origin': 0.0,
                'longitude_step': 0.5454545617103577
            },
            {
                'latitude': 78.6291732788086,
                'point_count': 664,
                'longitude_origin': 0.0,
                'longitude_step': 0.5421686768531799
            },
            {
                'latitude': 78.55887603759766,
                'point_count': 668,
                'longitude_origin': 0.0,
                'longitude_step': 0.538922131061554
            },
            {
                'latitude': 78.48857116699219,
                'point_count': 672,
                'longitude_origin': 0.0,
                'longitude_step': 0.5357142686843872
            },
            {
                'latitude': 78.41827392578125,
                'point_count': 676,
                'longitude_origin': 0.0,
                'longitude_step': 0.5325443744659424
            },
            {
                'latitude': 78.34797668457031,
                'point_count': 680,
                'longitude_origin': 0.0,
                'longitude_step': 0.529411792755127
            },
            {
                'latitude': 78.27767944335938,
                'point_count': 684,
                'longitude_origin': 0.0,
                'longitude_step': 0.5263158082962036
            },
            {
                'latitude': 78.20738220214844,
                'point_count': 688,
                'longitude_origin': 0.0,
                'longitude_step': 0.5232558250427246
            },
            {
                'latitude': 78.13707733154297,
                'point_count': 692,
                'longitude_origin': 0.0,
                'longitude_step': 0.5202311873435974
            },
            {
                'latitude': 78.06678009033203,
                'point_count': 696,
                'longitude_origin': 0.0,
                'longitude_step': 0.517241358757019
            },
            {
                'latitude': 77.9964828491211,
                'point_count': 700,
                'longitude_origin': 0.0,
                'longitude_step': 0.5142857432365417
            },
            {
                'latitude': 77.92618560791016,
                'point_count': 704,
                'longitude_origin': 0.0,
                'longitude_step': 0.5113636255264282
            },
            {
                'latitude': 77.85588836669922,
                'point_count': 708,
                'longitude_origin': 0.0,
                'longitude_step': 0.508474588394165
            },
            {
                'latitude': 77.78558349609375,
                'point_count': 712,
                'longitude_origin': 0.0,
                'longitude_step': 0.5056179761886597
            },
            {
                'latitude': 77.71528625488281,
                'point_count': 716,
                'longitude_origin': 0.0,
                'longitude_step': 0.5027933120727539
            },
            {
                'latitude': 77.64498901367188,
                'point_count': 720,
                'longitude_origin': 0.0,
                'longitude_step': 0.5
            },
            {
                'latitude': 77.57469177246094,
                'point_count': 724,
                'longitude_origin': 0.0,
                'longitude_step': 0.49723756313323975
            },
            {
                'latitude': 77.50439453125,
                'point_count': 728,
                'longitude_origin': 0.0,
                'longitude_step': 0.49450549483299255
            },
            {
                'latitude': 77.43408966064453,
                'point_count': 732,
                'longitude_origin': 0.0,
                'longitude_step': 0.49180328845977783
            },
            {
                'latitude': 77.3637924194336,
                'point_count': 736,
                'longitude_origin': 0.0,
                'longitude_step': 0.489130437374115
            },
            {
                'latitude': 77.29349517822266,
                'point_count': 740,
                'longitude_origin': 0.0,
                'longitude_step': 0.4864864945411682
            },
            {
                'latitude': 77.22319793701172,
                'point_count': 744,
                'longitude_origin': 0.0,
                'longitude_step': 0.4838709533214569
            },
            {
                'latitude': 77.15290069580078,
                'point_count': 748,
                'longitude_origin': 0.0,
                'longitude_step': 0.48128342628479004
            },
            {
                'latitude': 77.08259582519531,
                'point_count': 752,
                'longitude_origin': 0.0,
                'longitude_step': 0.478723406791687
            },
            {
                'latitude': 77.01229858398438,
                'point_count': 756,
                'longitude_origin': 0.0,
                'longitude_step': 0.4761904776096344
            },
            {
                'latitude': 76.94200134277344,
                'point_count': 760,
                'longitude_origin': 0.0,
                'longitude_step': 0.4736842215061188
            },
            {
                'latitude': 76.8717041015625,
                'point_count': 764,
                'longitude_origin': 0.0,
                'longitude_step': 0.4712041914463043
            },
            {
                'latitude': 76.80140686035156,
                'point_count': 768,
                'longitude_origin': 0.0,
                'longitude_step': 0.46875
            },
            {
                'latitude': 76.7311019897461,
                'point_count': 772,
                'longitude_origin': 0.0,
                'longitude_step': 0.4663212299346924
            },
            {
                'latitude': 76.66080474853516,
                'point_count': 776,
                'longitude_origin': 0.0,
                'longitude_step': 0.4639175236225128
            },
            {
                'latitude': 76.59050750732422,
                'point_count': 780,
                'longitude_origin': 0.0,
                'longitude_step': 0.4615384638309479
            },
            {
                'latitude': 76.52021026611328,
                'point_count': 784,
                'longitude_origin': 0.0,
                'longitude_step': 0.4591836631298065
            },
            {
                'latitude': 76.44991302490234,
                'point_count': 788,
                'longitude_origin': 0.0,
                'longitude_step': 0.4568527936935425
            },
            {
                'latitude': 76.37960815429688,
                'point_count': 792,
                'longitude_origin': 0.0,
                'longitude_step': 0.4545454680919647
            },
            {
                'latitude': 76.30931091308594,
                'point_count': 796,
                'longitude_origin': 0.0,
                'longitude_step': 0.4522612988948822
            },
            {
                'latitude': 76.239013671875,
                'point_count': 800,
                'longitude_origin': 0.0,
                'longitude_step': 0.44999998807907104
            },
            {
                'latitude': 76.16871643066406,
                'point_count': 804,
                'longitude_origin': 0.0,
                'longitude_step': 0.447761207818985
            },
            {
                'latitude': 76.09841918945312,
                'point_count': 808,
                'longitude_origin': 0.0,
                'longitude_step': 0.4455445408821106
            },
            {
                'latitude': 76.02811431884766,
                'point_count': 812,
                'longitude_origin': 0.0,
                'longitude_step': 0.4433497488498688
            },
            {
                'latitude': 75.95781707763672,
                'point_count': 816,
                'longitude_origin': 0.0,
                'longitude_step': 0.44117647409439087
            },
            {
                'latitude': 75.88751983642578,
                'point_count': 820,
                'longitude_origin': 0.0,
                'longitude_step': 0.4390243887901306
            },
            {
                'latitude': 75.81722259521484,
                'point_count': 824,
                'longitude_origin': 0.0,
                'longitude_step': 0.43689319491386414
            },
            {
                'latitude': 75.7469253540039,
                'point_count': 828,
                'longitude_origin': 0.0,
                'longitude_step': 0.43478259444236755
            },
            {
                'latitude': 75.67662048339844,
                'point_count': 832,
                'longitude_origin': 0.0,
                'longitude_step': 0.4326923191547394
            },
            {
                'latitude': 75.6063232421875,
                'point_count': 836,
                'longitude_origin': 0.0,
                'longitude_step': 0.43062201142311096
            },
            {
                'latitude': 75.53602600097656,
                'point_count': 840,
                'longitude_origin': 0.0,
                'longitude_step': 0.4285714328289032
            },
            {
                'latitude': 75.46572875976562,
                'point_count': 844,
                'longitude_origin': 0.0,
                'longitude_step': 0.4265402853488922
            },
            {
                'latitude': 75.39543151855469,
                'point_count': 848,
                'longitude_origin': 0.0,
                'longitude_step': 0.4245283007621765
            },
            {
                'latitude': 75.32512664794922,
                'point_count': 852,
                'longitude_origin': 0.0,
                'longitude_step': 0.4225352108478546
            },
            {
                'latitude': 75.25482940673828,
                'point_count': 856,
                'longitude_origin': 0.0,
                'longitude_step': 0.420560747385025
            },
            {
                'latitude': 75.18453216552734,
                'point_count': 860,
                'longitude_origin': 0.0,
                'longitude_step': 0.41860464215278625
            },
            {
                'latitude': 75.1142349243164,
                'point_count': 864,
                'longitude_origin': 0.0,
                'longitude_step': 0.4166666567325592
            },
            {
                'latitude': 75.04393768310547,
                'point_count': 868,
                'longitude_origin': 0.0,
                'longitude_step': 0.41474655270576477
            },
            {
                'latitude': 74.9736328125,
                'point_count': 872,
                'longitude_origin': 0.0,
                'longitude_step': 0.4128440320491791
            },
            {
                'latitude': 74.90333557128906,
                'point_count': 876,
                'longitude_origin': 0.0,
                'longitude_step': 0.4109589159488678
            },
            {
                'latitude': 74.83303833007812,
                'point_count': 880,
                'longitude_origin': 0.0,
                'longitude_step': 0.40909090638160706
            },
            {
                'latitude': 74.76274108886719,
                'point_count': 884,
                'longitude_origin': 0.0,
                'longitude_step': 0.4072398245334625
            },
            {
                'latitude': 74.69244384765625,
                'point_count': 888,
                'longitude_origin': 0.0,
                'longitude_step': 0.4054054021835327
            },
            {
                'latitude': 74.62213897705078,
                'point_count': 892,
                'longitude_origin': 0.0,
                'longitude_step': 0.4035874307155609
            },
            {
                'latitude': 74.55184173583984,
                'point_count': 896,
                'longitude_origin': 0.0,
                'longitude_step': 0.4017857015132904
            },
            {
                'latitude': 74.4815444946289,
                'point_count': 900,
                'longitude_origin': 0.0,
                'longitude_step': 0.4000000059604645
            },
            {
                'latitude': 74.41124725341797,
                'point_count': 904,
                'longitude_origin': 0.0,
                'longitude_step': 0.39823007583618164
            },
            {
                'latitude': 74.34095001220703,
                'point_count': 908,
                'longitude_origin': 0.0,
                'longitude_step': 0.39647576212882996
            },
            {
                'latitude': 74.27064514160156,
                'point_count': 912,
                'longitude_origin': 0.0,
                'longitude_step': 0.3947368562221527
            },
            {
                'latitude': 74.20034790039062,
                'point_count': 916,
                'longitude_origin': 0.0,
                'longitude_step': 0.3930130898952484
            },
            {
                'latitude': 74.13005065917969,
                'point_count': 920,
                'longitude_origin': 0.0,
                'longitude_step': 0.3913043439388275
            },
            {
                'latitude': 74.05975341796875,
                'point_count': 924,
                'longitude_origin': 0.0,
                'longitude_step': 0.3896103799343109
            },
            {
                'latitude': 73.98945617675781,
                'point_count': 928,
                'longitude_origin': 0.0,
                'longitude_step': 0.38793104887008667
            },
            {
                'latitude': 73.91915130615234,
                'point_count': 932,
                'longitude_origin': 0.0,
                'longitude_step': 0.3862660825252533
            },
            {
                'latitude': 73.8488540649414,
                'point_count': 936,
                'longitude_origin': 0.0,
                'longitude_step': 0.38461539149284363
            },
            {
                'latitude': 73.77855682373047,
                'point_count': 940,
                'longitude_origin': 0.0,
                'longitude_step': 0.38297873735427856
            },
            {
                'latitude': 73.70825958251953,
                'point_count': 944,
                'longitude_origin': 0.0,
                'longitude_step': 0.3813559412956238
            },
            {
                'latitude': 73.6379623413086,
                'point_count': 948,
                'longitude_origin': 0.0,
                'longitude_step': 0.37974682450294495
            },
            {
                'latitude': 73.56765747070312,
                'point_count': 952,
                'longitude_origin': 0.0,
                'longitude_step': 0.3781512677669525
            },
            {
                'latitude': 73.49736022949219,
                'point_count': 956,
                'longitude_origin': 0.0,
                'longitude_step': 0.3765690326690674
            },
            {
                'latitude': 73.42706298828125,
                'point_count': 960,
                'longitude_origin': 0.0,
                'longitude_step': 0.375
            },
            {
                'latitude': 73.35676574707031,
                'point_count': 964,
                'longitude_origin': 0.0,
                'longitude_step': 0.37344399094581604
            },
            {
                'latitude': 73.28646850585938,
                'point_count': 968,
                'longitude_origin': 0.0,
                'longitude_step': 0.3719008266925812
            },
            {
                'latitude': 73.2161636352539,
                'point_count': 972,
                'longitude_origin': 0.0,
                'longitude_step': 0.37037035822868347
            },
            {
                'latitude': 73.14586639404297,
                'point_count': 976,
                'longitude_origin': 0.0,
                'longitude_step': 0.3688524663448334
            },
            {
                'latitude': 73.07556915283203,
                'point_count': 980,
                'longitude_origin': 0.0,
                'longitude_step': 0.36734694242477417
            },
            {
                'latitude': 73.0052719116211,
                'point_count': 984,
                'longitude_origin': 0.0,
                'longitude_step': 0.3658536672592163
            },
            {
                'latitude': 72.93497467041016,
                'point_count': 988,
                'longitude_origin': 0.0,
                'longitude_step': 0.36437246203422546
            },
            {
                'latitude': 72.86466979980469,
                'point_count': 992,
                'longitude_origin': 0.0,
                'longitude_step': 0.3629032373428345
            },
            {
                'latitude': 72.79437255859375,
                'point_count': 996,
                'longitude_origin': 0.0,
                'longitude_step': 0.3614457845687866
            },
            {
                'latitude': 72.72407531738281,
                'point_count': 1000,
                'longitude_origin': 0.0,
                'longitude_step': 0.36000001430511475
            },
            {
                'latitude': 72.65377807617188,
                'point_count': 1004,
                'longitude_origin': 0.0,
                'longitude_step': 0.3585657477378845
            },
            {
                'latitude': 72.58348083496094,
                'point_count': 1008,
                'longitude_origin': 0.0,
                'longitude_step': 0.3571428656578064
            },
            {
                'latitude': 72.51317596435547,
                'point_count': 1012,
                'longitude_origin': 0.0,
                'longitude_step': 0.35573121905326843
            },
            {
                'latitude': 72.44287872314453,
                'point_count': 1016,
                'longitude_origin': 0.0,
                'longitude_step': 0.35433071851730347
            },
            {
                'latitude': 72.3725814819336,
                'point_count': 1020,
                'longitude_origin': 0.0,
                'longitude_step': 0.3529411852359772
            },
            {
                'latitude': 72.30228424072266,
                'point_count': 1024,
                'longitude_origin': 0.0,
                'longitude_step': 0.3515625
            },
            {
                'latitude': 72.23198699951172,
                'point_count': 1028,
                'longitude_origin': 0.0,
                'longitude_step': 0.3501945436000824
            },
            {
                'latitude': 72.16168212890625,
                'point_count': 1032,
                'longitude_origin': 0.0,
                'longitude_step': 0.3488371968269348
            },
            {
                'latitude': 72.09138488769531,
                'point_count': 1036,
                'longitude_origin': 0.0,
                'longitude_step': 0.3474903404712677
            },
            {
                'latitude': 72.02108764648438,
                'point_count': 1040,
                'longitude_origin': 0.0,
                'longitude_step': 0.3461538553237915
            },
            {
                'latitude': 71.95079040527344,
                'point_count': 1044,
                'longitude_origin': 0.0,
                'longitude_step': 0.3448275923728943
            },
            {
                'latitude': 71.8804931640625,
                'point_count': 1048,
                'longitude_origin': 0.0,
                'longitude_step': 0.3435114622116089
            },
            {
                'latitude': 71.81018829345703,
                'point_count': 1052,
                'longitude_origin': 0.0,
                'longitude_step': 0.34220531582832336
            },
            {
                'latitude': 71.7398910522461,
                'point_count': 1056,
                'longitude_origin': 0.0,
                'longitude_step': 0.34090909361839294
            },
            {
                'latitude': 71.66959381103516,
                'point_count': 1060,
                'longitude_origin': 0.0,
                'longitude_step': 0.3396226465702057
            },
            {
                'latitude': 71.59929656982422,
                'point_count': 1064,
                'longitude_origin': 0.0,
                'longitude_step': 0.33834585547447205
            },
            {
                'latitude': 71.52899932861328,
                'point_count': 1068,
                'longitude_origin': 0.0,
                'longitude_step': 0.33707866072654724
            },
            {
                'latitude': 71.45869445800781,
                'point_count': 1072,
                'longitude_origin': 0.0,
                'longitude_step': 0.33582088351249695
            },
            {
                'latitude': 71.38839721679688,
                'point_count': 1076,
                'longitude_origin': 0.0,
                'longitude_step': 0.3345724940299988
            },
            {
                'latitude': 71.31809997558594,
                'point_count': 1080,
                'longitude_origin': 0.0,
                'longitude_step': 0.3333333432674408
            },
            {
                'latitude': 71.247802734375,
                'point_count': 1084,
                'longitude_origin': 0.0,
                'longitude_step': 0.33210331201553345
            },
            {
                'latitude': 71.17750549316406,
                'point_count': 1088,
                'longitude_origin': 0.0,
                'longitude_step': 0.33088234066963196
            },
            {
                'latitude': 71.1072006225586,
                'point_count': 1092,
                'longitude_origin': 0.0,
                'longitude_step': 0.32967033982276917
            },
            {
                'latitude': 71.03690338134766,
                'point_count': 1096,
                'longitude_origin': 0.0,
                'longitude_step': 0.32846716046333313
            },
            {
                'latitude': 70.96660614013672,
                'point_count': 1100,
                'longitude_origin': 0.0,
                'longitude_step': 0.3272727131843567
            },
            {
                'latitude': 70.89630889892578,
                'point_count': 1104,
                'longitude_origin': 0.0,
                'longitude_step': 0.32608696818351746
            },
            {
                'latitude': 70.82601165771484,
                'point_count': 1108,
                'longitude_origin': 0.0,
                'longitude_step': 0.3249097466468811
            },
            {
                'latitude': 70.75570678710938,
                'point_count': 1112,
                'longitude_origin': 0.0,
                'longitude_step': 0.32374101877212524
            },
            {
                'latitude': 70.68540954589844,
                'point_count': 1116,
                'longitude_origin': 0.0,
                'longitude_step': 0.32258063554763794
            },
            {
                'latitude': 70.6151123046875,
                'point_count': 1120,
                'longitude_origin': 0.0,
                'longitude_step': 0.3214285671710968
            },
            {
                'latitude': 70.54481506347656,
                'point_count': 1124,
                'longitude_origin': 0.0,
                'longitude_step': 0.3202846944332123
            },
            {
                'latitude': 70.47451782226562,
                'point_count': 1128,
                'longitude_origin': 0.0,
                'longitude_step': 0.3191489279270172
            },
            {
                'latitude': 70.40421295166016,
                'point_count': 1132,
                'longitude_origin': 0.0,
                'longitude_step': 0.3180212080478668
            },
            {
                'latitude': 70.33391571044922,
                'point_count': 1136,
                'longitude_origin': 0.0,
                'longitude_step': 0.31690141558647156
            },
            {
                'latitude': 70.26361846923828,
                'point_count': 1140,
                'longitude_origin': 0.0,
                'longitude_step': 0.31578946113586426
            },
            {
                'latitude': 70.19332122802734,
                'point_count': 1144,
                'longitude_origin': 0.0,
                'longitude_step': 0.31468531489372253
            },
            {
                'latitude': 70.1230239868164,
                'point_count': 1148,
                'longitude_origin': 0.0,
                'longitude_step': 0.31358885765075684
            },
            {
                'latitude': 70.05271911621094,
                'point_count': 1152,
                'longitude_origin': 0.0,
                'longitude_step': 0.3125
            },
            {
                'latitude': 69.982421875,
                'point_count': 1156,
                'longitude_origin': 0.0,
                'longitude_step': 0.31141868233680725
            },
            {
                'latitude': 69.91212463378906,
                'point_count': 1160,
                'longitude_origin': 0.0,
                'longitude_step': 0.3103448152542114
            },
            {
                'latitude': 69.84182739257812,
                'point_count': 1164,
                'longitude_origin': 0.0,
                'longitude_step': 0.30927833914756775
            },
            {
                'latitude': 69.77153015136719,
                'point_count': 1168,
                'longitude_origin': 0.0,
                'longitude_step': 0.30821916460990906
            },
            {
                'latitude': 69.70122528076172,
                'point_count': 1172,
                'longitude_origin': 0.0,
                'longitude_step': 0.3071672320365906
            },
            {
                'latitude': 69.63092803955078,
                'point_count': 1176,
                'longitude_origin': 0.0,
                'longitude_step': 0.30612245202064514
            },
            {
                'latitude': 69.56063079833984,
                'point_count': 1180,
                'longitude_origin': 0.0,
                'longitude_step': 0.3050847351551056
            },
            {
                'latitude': 69.4903335571289,
                'point_count': 1184,
                'longitude_origin': 0.0,
                'longitude_step': 0.30405405163764954
            },
            {
                'latitude': 69.42003631591797,
                'point_count': 1188,
                'longitude_origin': 0.0,
                'longitude_step': 0.3030303120613098
            },
            {
                'latitude': 69.3497314453125,
                'point_count': 1192,
                'longitude_origin': 0.0,
                'longitude_step': 0.30201342701911926
            },
            {
                'latitude': 69.27943420410156,
                'point_count': 1196,
                'longitude_origin': 0.0,
                'longitude_step': 0.3010033369064331
            },
            {
                'latitude': 69.20913696289062,
                'point_count': 1200,
                'longitude_origin': 0.0,
                'longitude_step': 0.30000001192092896
            },
            {
                'latitude': 69.13883972167969,
                'point_count': 1204,
                'longitude_origin': 0.0,
                'longitude_step': 0.29900333285331726
            },
            {
                'latitude': 69.06854248046875,
                'point_count': 1208,
                'longitude_origin': 0.0,
                'longitude_step': 0.29801324009895325
            },
            {
                'latitude': 68.99823760986328,
                'point_count': 1212,
                'longitude_origin': 0.0,
                'longitude_step': 0.2970297038555145
            },
            {
                'latitude': 68.92794036865234,
                'point_count': 1216,
                'longitude_origin': 0.0,
                'longitude_step': 0.29605263471603394
            },
            {
                'latitude': 68.8576431274414,
                'point_count': 1220,
                'longitude_origin': 0.0,
                'longitude_step': 0.2950819730758667
            },
            {
                'latitude': 68.78734588623047,
                'point_count': 1224,
                'longitude_origin': 0.0,
                'longitude_step': 0.29411765933036804
            },
            {
                'latitude': 68.71704864501953,
                'point_count': 1228,
                'longitude_origin': 0.0,
                'longitude_step': 0.2931596040725708
            },
            {
                'latitude': 68.64674377441406,
                'point_count': 1232,
                'longitude_origin': 0.0,
                'longitude_step': 0.2922077775001526
            },
            {
                'latitude': 68.57644653320312,
                'point_count': 1236,
                'longitude_origin': 0.0,
                'longitude_step': 0.291262149810791
            },
            {
                'latitude': 68.50614929199219,
                'point_count': 1240,
                'longitude_origin': 0.0,
                'longitude_step': 0.29032257199287415
            },
            {
                'latitude': 68.43585205078125,
                'point_count': 1244,
                'longitude_origin': 0.0,
                'longitude_step': 0.28938907384872437
            },
            {
                'latitude': 68.36555480957031,
                'point_count': 1248,
                'longitude_origin': 0.0,
                'longitude_step': 0.2884615361690521
            },
            {
                'latitude': 68.29524993896484,
                'point_count': 1252,
                'longitude_origin': 0.0,
                'longitude_step': 0.28753992915153503
            },
            {
                'latitude': 68.2249526977539,
                'point_count': 1256,
                'longitude_origin': 0.0,
                'longitude_step': 0.2866241931915283
            },
            {
                'latitude': 68.15465545654297,
                'point_count': 1260,
                'longitude_origin': 0.0,
                'longitude_step': 0.2857142984867096
            },
            {
                'latitude': 68.08435821533203,
                'point_count': 1264,
                'longitude_origin': 0.0,
                'longitude_step': 0.2848101258277893
            },
            {
                'latitude': 68.0140609741211,
                'point_count': 1268,
                'longitude_origin': 0.0,
                'longitude_step': 0.28391167521476746
            },
            {
                'latitude': 67.94375610351562,
                'point_count': 1272,
                'longitude_origin': 0.0,
                'longitude_step': 0.2830188572406769
            },
            {
                'latitude': 67.87345886230469,
                'point_count': 1276,
                'longitude_origin': 0.0,
                'longitude_step': 0.2821316719055176
            },
            {
                'latitude': 67.80316162109375,
                'point_count': 1280,
                'longitude_origin': 0.0,
                'longitude_step': 0.28125
            },
            {
                'latitude': 67.73286437988281,
                'point_count': 1284,
                'longitude_origin': 0.0,
                'longitude_step': 0.28037384152412415
            },
            {
                'latitude': 67.66256713867188,
                'point_count': 1288,
                'longitude_origin': 0.0,
                'longitude_step': 0.27950310707092285
            },
            {
                'latitude': 67.5922622680664,
                'point_count': 1292,
                'longitude_origin': 0.0,
                'longitude_step': 0.27863776683807373
            },
            {
                'latitude': 67.52196502685547,
                'point_count': 1296,
                'longitude_origin': 0.0,
                'longitude_step': 0.2777777910232544
            },
            {
                'latitude': 67.45166778564453,
                'point_count': 1300,
                'longitude_origin': 0.0,
                'longitude_step': 0.2769230902194977
            },
            {
                'latitude': 67.3813705444336,
                'point_count': 1304,
                'longitude_origin': 0.0,
                'longitude_step': 0.2760736048221588
            },
            {
                'latitude': 67.31107330322266,
                'point_count': 1308,
                'longitude_origin': 0.0,
                'longitude_step': 0.2752293646335602
            },
            {
                'latitude': 67.24076843261719,
                'point_count': 1312,
                'longitude_origin': 0.0,
                'longitude_step': 0.27439025044441223
            },
            {
                'latitude': 67.17047119140625,
                'point_count': 1316,
                'longitude_origin': 0.0,
                'longitude_step': 0.2735562324523926
            },
            {
                'latitude': 67.10017395019531,
                'point_count': 1320,
                'longitude_origin': 0.0,
                'longitude_step': 0.27272728085517883
            },
            {
                'latitude': 67.02987670898438,
                'point_count': 1324,
                'longitude_origin': 0.0,
                'longitude_step': 0.2719033360481262
            },
            {
                'latitude': 66.95957946777344,
                'point_count': 1328,
                'longitude_origin': 0.0,
                'longitude_step': 0.27108433842658997
            },
            {
                'latitude': 66.88927459716797,
                'point_count': 1332,
                'longitude_origin': 0.0,
                'longitude_step': 0.2702702581882477
            },
            {
                'latitude': 66.81897735595703,
                'point_count': 1336,
                'longitude_origin': 0.0,
                'longitude_step': 0.269461065530777
            },
            {
                'latitude': 66.7486801147461,
                'point_count': 1340,
                'longitude_origin': 0.0,
                'longitude_step': 0.26865673065185547
            },
            {
                'latitude': 66.67838287353516,
                'point_count': 1344,
                'longitude_origin': 0.0,
                'longitude_step': 0.2678571343421936
            },
            {
                'latitude': 66.60808563232422,
                'point_count': 1348,
                'longitude_origin': 0.0,
                'longitude_step': 0.26706230640411377
            },
            {
                'latitude': 66.53778076171875,
                'point_count': 1352,
                'longitude_origin': 0.0,
                'longitude_step': 0.2662721872329712
            },
            {
                'latitude': 66.46748352050781,
                'point_count': 1356,
                'longitude_origin': 0.0,
                'longitude_step': 0.2654867172241211
            },
            {
                'latitude': 66.39718627929688,
                'point_count': 1360,
                'longitude_origin': 0.0,
                'longitude_step': 0.2647058963775635
            },
            {
                'latitude': 66.32688903808594,
                'point_count': 1364,
                'longitude_origin': 0.0,
                'longitude_step': 0.2639296054840088
            },
            {
                'latitude': 66.256591796875,
                'point_count': 1368,
                'longitude_origin': 0.0,
                'longitude_step': 0.2631579041481018
            },
            {
                'latitude': 66.18628692626953,
                'point_count': 1372,
                'longitude_origin': 0.0,
                'longitude_step': 0.262390673160553
            },
            {
                'latitude': 66.1159896850586,
                'point_count': 1376,
                'longitude_origin': 0.0,
                'longitude_step': 0.2616279125213623
            },
            {
                'latitude': 66.04569244384766,
                'point_count': 1380,
                'longitude_origin': 0.0,
                'longitude_step': 0.260869562625885
            },
            {
                'latitude': 65.97539520263672,
                'point_count': 1384,
                'longitude_origin': 0.0,
                'longitude_step': 0.2601155936717987
            },
            {
                'latitude': 65.90509796142578,
                'point_count': 1388,
                'longitude_origin': 0.0,
                'longitude_step': 0.2593660056591034
            },
            {
                'latitude': 65.83479309082031,
                'point_count': 1392,
                'longitude_origin': 0.0,
                'longitude_step': 0.2586206793785095
            },
            {
                'latitude': 65.76449584960938,
                'point_count': 1396,
                'longitude_origin': 0.0,
                'longitude_step': 0.2578796446323395
            },
            {
                'latitude': 65.69419860839844,
                'point_count': 1400,
                'longitude_origin': 0.0,
                'longitude_step': 0.2571428716182709
            },
            {
                'latitude': 65.6239013671875,
                'point_count': 1404,
                'longitude_origin': 0.0,
                'longitude_step': 0.25641027092933655
            },
            {
                'latitude': 65.55360412597656,
                'point_count': 1408,
                'longitude_origin': 0.0,
                'longitude_step': 0.2556818127632141
            },
            {
                'latitude': 65.4832992553711,
                'point_count': 1412,
                'longitude_origin': 0.0,
                'longitude_step': 0.25495749711990356
            },
            {
                'latitude': 65.41300201416016,
                'point_count': 1416,
                'longitude_origin': 0.0,
                'longitude_step': 0.2542372941970825
            },
            {
                'latitude': 65.34270477294922,
                'point_count': 1420,
                'longitude_origin': 0.0,
                'longitude_step': 0.2535211145877838
            },
            {
                'latitude': 65.27240753173828,
                'point_count': 1424,
                'longitude_origin': 0.0,
                'longitude_step': 0.25280898809432983
            },
            {
                'latitude': 65.20211029052734,
                'point_count': 1428,
                'longitude_origin': 0.0,
                'longitude_step': 0.2521008551120758
            },
            {
                'latitude': 65.13180541992188,
                'point_count': 1432,
                'longitude_origin': 0.0,
                'longitude_step': 0.25139665603637695
            },
            {
                'latitude': 65.06150817871094,
                'point_count': 1436,
                'longitude_origin': 0.0,
                'longitude_step': 0.2506963908672333
            },
            {
                'latitude': 64.9912109375,
                'point_count': 1440,
                'longitude_origin': 0.0,
                'longitude_step': 0.25
            },
            {
                'latitude': 64.92091369628906,
                'point_count': 1444,
                'longitude_origin': 0.0,
                'longitude_step': 0.24930748343467712
            },
            {
                'latitude': 64.85061645507812,
                'point_count': 1448,
                'longitude_origin': 0.0,
                'longitude_step': 0.24861878156661987
            },
            {
                'latitude': 64.78031158447266,
                'point_count': 1452,
                'longitude_origin': 0.0,
                'longitude_step': 0.24793387949466705
            },
            {
                'latitude': 64.71001434326172,
                'point_count': 1456,
                'longitude_origin': 0.0,
                'longitude_step': 0.24725274741649628
            },
            {
                'latitude': 64.63971710205078,
                'point_count': 1460,
                'longitude_origin': 0.0,
                'longitude_step': 0.24657534062862396
            },
            {
                'latitude': 64.56941986083984,
                'point_count': 1464,
                'longitude_origin': 0.0,
                'longitude_step': 0.24590164422988892
            },
            {
                'latitude': 64.4991226196289,
                'point_count': 1468,
                'longitude_origin': 0.0,
                'longitude_step': 0.24523161351680756
            },
            {
                'latitude': 64.42881774902344,
                'point_count': 1472,
                'longitude_origin': 0.0,
                'longitude_step': 0.2445652186870575
            },
            {
                'latitude': 64.3585205078125,
                'point_count': 1476,
                'longitude_origin': 0.0,
                'longitude_step': 0.24390244483947754
            },
            {
                'latitude': 64.28822326660156,
                'point_count': 1480,
                'longitude_origin': 0.0,
                'longitude_step': 0.2432432472705841
            },
            {
                'latitude': 64.21792602539062,
                'point_count': 1484,
                'longitude_origin': 0.0,
                'longitude_step': 0.2425875961780548
            },
            {
                'latitude': 64.14762878417969,
                'point_count': 1488,
                'longitude_origin': 0.0,
                'longitude_step': 0.24193547666072845
            },
            {
                'latitude': 64.07732391357422,
                'point_count': 1492,
                'longitude_origin': 0.0,
                'longitude_step': 0.24128685891628265
            },
            {
                'latitude': 64.00702667236328,
                'point_count': 1496,
                'longitude_origin': 0.0,
                'longitude_step': 0.24064171314239502
            },
            {
                'latitude': 63.936729431152344,
                'point_count': 1500,
                'longitude_origin': 0.0,
                'longitude_step': 0.23999999463558197
            },
            {
                'latitude': 63.866432189941406,
                'point_count': 1504,
                'longitude_origin': 0.0,
                'longitude_step': 0.2393617033958435
            },
            {
                'latitude': 63.7961311340332,
                'point_count': 1508,
                'longitude_origin': 0.0,
                'longitude_step': 0.23872679471969604
            },
            {
                'latitude': 63.725833892822266,
                'point_count': 1512,
                'longitude_origin': 0.0,
                'longitude_step': 0.2380952388048172
            },
            {
                'latitude': 63.65553283691406,
                'point_count': 1516,
                'longitude_origin': 0.0,
                'longitude_step': 0.23746702075004578
            },
            {
                'latitude': 63.585235595703125,
                'point_count': 1520,
                'longitude_origin': 0.0,
                'longitude_step': 0.2368421107530594
            },
            {
                'latitude': 63.51493835449219,
                'point_count': 1524,
                'longitude_origin': 0.0,
                'longitude_step': 0.23622047901153564
            },
            {
                'latitude': 63.444637298583984,
                'point_count': 1528,
                'longitude_origin': 0.0,
                'longitude_step': 0.23560209572315216
            },
            {
                'latitude': 63.37434005737305,
                'point_count': 1532,
                'longitude_origin': 0.0,
                'longitude_step': 0.23498694598674774
            },
            {
                'latitude': 63.304039001464844,
                'point_count': 1536,
                'longitude_origin': 0.0,
                'longitude_step': 0.234375
            },
            {
                'latitude': 63.233741760253906,
                'point_count': 1540,
                'longitude_origin': 0.0,
                'longitude_step': 0.23376622796058655
            },
            {
                'latitude': 63.16344451904297,
                'point_count': 1544,
                'longitude_origin': 0.0,
                'longitude_step': 0.2331606149673462
            },
            {
                'latitude': 63.093143463134766,
                'point_count': 1548,
                'longitude_origin': 0.0,
                'longitude_step': 0.23255814611911774
            },
            {
                'latitude': 63.02284622192383,
                'point_count': 1552,
                'longitude_origin': 0.0,
                'longitude_step': 0.2319587618112564
            },
            {
                'latitude': 62.952545166015625,
                'point_count': 1556,
                'longitude_origin': 0.0,
                'longitude_step': 0.2313624620437622
            },
            {
                'latitude': 62.88224792480469,
                'point_count': 1560,
                'longitude_origin': 0.0,
                'longitude_step': 0.23076923191547394
            },
            {
                'latitude': 62.81195068359375,
                'point_count': 1564,
                'longitude_origin': 0.0,
                'longitude_step': 0.23017902672290802
            },
            {
                'latitude': 62.74164962768555,
                'point_count': 1568,
                'longitude_origin': 0.0,
                'longitude_step': 0.22959183156490326
            },
            {
                'latitude': 62.67135238647461,
                'point_count': 1572,
                'longitude_origin': 0.0,
                'longitude_step': 0.22900763154029846
            },
            {
                'latitude': 62.601051330566406,
                'point_count': 1576,
                'longitude_origin': 0.0,
                'longitude_step': 0.22842639684677124
            },
            {
                'latitude': 62.53075408935547,
                'point_count': 1580,
                'longitude_origin': 0.0,
                'longitude_step': 0.2278480976819992
            },
            {
                'latitude': 62.46045684814453,
                'point_count': 1584,
                'longitude_origin': 0.0,
                'longitude_step': 0.22727273404598236
            },
            {
                'latitude': 62.39015579223633,
                'point_count': 1588,
                'longitude_origin': 0.0,
                'longitude_step': 0.22670024633407593
            },
            {
                'latitude': 62.31985855102539,
                'point_count': 1592,
                'longitude_origin': 0.0,
                'longitude_step': 0.2261306494474411
            },
            {
                'latitude': 62.24955749511719,
                'point_count': 1596,
                'longitude_origin': 0.0,
                'longitude_step': 0.2255639135837555
            },
            {
                'latitude': 62.17926025390625,
                'point_count': 1600,
                'longitude_origin': 0.0,
                'longitude_step': 0.22499999403953552
            },
            {
                'latitude': 62.10896301269531,
                'point_count': 1604,
                'longitude_origin': 0.0,
                'longitude_step': 0.22443890571594238
            },
            {
                'latitude': 62.03866195678711,
                'point_count': 1608,
                'longitude_origin': 0.0,
                'longitude_step': 0.2238806039094925
            },
            {
                'latitude': 61.96836471557617,
                'point_count': 1612,
                'longitude_origin': 0.0,
                'longitude_step': 0.22332505881786346
            },
            {
                'latitude': 61.89806365966797,
                'point_count': 1616,
                'longitude_origin': 0.0,
                'longitude_step': 0.2227722704410553
            },
            {
                'latitude': 61.82776641845703,
                'point_count': 1620,
                'longitude_origin': 0.0,
                'longitude_step': 0.2222222238779068
            },
            {
                'latitude': 61.757469177246094,
                'point_count': 1624,
                'longitude_origin': 0.0,
                'longitude_step': 0.2216748744249344
            },
            {
                'latitude': 61.68716812133789,
                'point_count': 1628,
                'longitude_origin': 0.0,
                'longitude_step': 0.22113022208213806
            },
            {
                'latitude': 61.61687088012695,
                'point_count': 1632,
                'longitude_origin': 0.0,
                'longitude_step': 0.22058823704719543
            },
            {
                'latitude': 61.54656982421875,
                'point_count': 1636,
                'longitude_origin': 0.0,
                'longitude_step': 0.2200489044189453
            },
            {
                'latitude': 61.47627258300781,
                'point_count': 1640,
                'longitude_origin': 0.0,
                'longitude_step': 0.2195121943950653
            },
            {
                'latitude': 61.405975341796875,
                'point_count': 1644,
                'longitude_origin': 0.0,
                'longitude_step': 0.21897810697555542
            },
            {
                'latitude': 61.33567428588867,
                'point_count': 1648,
                'longitude_origin': 0.0,
                'longitude_step': 0.21844659745693207
            },
            {
                'latitude': 61.265377044677734,
                'point_count': 1652,
                'longitude_origin': 0.0,
                'longitude_step': 0.21791768074035645
            },
            {
                'latitude': 61.19507598876953,
                'point_count': 1656,
                'longitude_origin': 0.0,
                'longitude_step': 0.21739129722118378
            },
            {
                'latitude': 61.124778747558594,
                'point_count': 1660,
                'longitude_origin': 0.0,
                'longitude_step': 0.21686747670173645
            },
            {
                'latitude': 61.054481506347656,
                'point_count': 1664,
                'longitude_origin': 0.0,
                'longitude_step': 0.2163461595773697
            },
            {
                'latitude': 60.98418045043945,
                'point_count': 1668,
                'longitude_origin': 0.0,
                'longitude_step': 0.2158273309469223
            },
            {
                'latitude': 60.913883209228516,
                'point_count': 1672,
                'longitude_origin': 0.0,
                'longitude_step': 0.21531100571155548
            },
            {
                'latitude': 60.84358215332031,
                'point_count': 1676,
                'longitude_origin': 0.0,
                'longitude_step': 0.21479713916778564
            },
            {
                'latitude': 60.773284912109375,
                'point_count': 1680,
                'longitude_origin': 0.0,
                'longitude_step': 0.2142857164144516
            },
            {
                'latitude': 60.70298767089844,
                'point_count': 1684,
                'longitude_origin': 0.0,
                'longitude_step': 0.21377672255039215
            },
            {
                'latitude': 60.632686614990234,
                'point_count': 1688,
                'longitude_origin': 0.0,
                'longitude_step': 0.2132701426744461
            },
            {
                'latitude': 60.5623893737793,
                'point_count': 1692,
                'longitude_origin': 0.0,
                'longitude_step': 0.21276596188545227
            },
            {
                'latitude': 60.492088317871094,
                'point_count': 1696,
                'longitude_origin': 0.0,
                'longitude_step': 0.21226415038108826
            },
            {
                'latitude': 60.421791076660156,
                'point_count': 1700,
                'longitude_origin': 0.0,
                'longitude_step': 0.21176470816135406
            },
            {
                'latitude': 60.35149383544922,
                'point_count': 1704,
                'longitude_origin': 0.0,
                'longitude_step': 0.2112676054239273
            },
            {
                'latitude': 60.281192779541016,
                'point_count': 1708,
                'longitude_origin': 0.0,
                'longitude_step': 0.2107728272676468
            },
            {
                'latitude': 60.21089553833008,
                'point_count': 1712,
                'longitude_origin': 0.0,
                'longitude_step': 0.2102803736925125
            },
            {
                'latitude': 60.140594482421875,
                'point_count': 1716,
                'longitude_origin': 0.0,
                'longitude_step': 0.2097902148962021
            },
            {
                'latitude': 60.07029724121094,
                'point_count': 1720,
                'longitude_origin': 0.0,
                'longitude_step': 0.20930232107639313
            },
            {
                'latitude': 60.0,
                'point_count': 1724,
                'longitude_origin': 0.0,
                'longitude_step': 0.20881670713424683
            },
            {
                'latitude': 59.9296989440918,
                'point_count': 1728,
                'longitude_origin': 0.0,
                'longitude_step': 0.2083333283662796
            },
            {
                'latitude': 59.85940170288086,
                'point_count': 1732,
                'longitude_origin': 0.0,
                'longitude_step': 0.20785219967365265
            },
            {
                'latitude': 59.789100646972656,
                'point_count': 1736,
                'longitude_origin': 0.0,
                'longitude_step': 0.20737327635288239
            },
            {
                'latitude': 59.71880340576172,
                'point_count': 1740,
                'longitude_origin': 0.0,
                'longitude_step': 0.2068965584039688
            },
            {
                'latitude': 59.64850616455078,
                'point_count': 1744,
                'longitude_origin': 0.0,
                'longitude_step': 0.20642201602458954
            },
            {
                'latitude': 59.57820510864258,
                'point_count': 1748,
                'longitude_origin': 0.0,
                'longitude_step': 0.20594966411590576
            },
            {
                'latitude': 59.50790786743164,
                'point_count': 1752,
                'longitude_origin': 0.0,
                'longitude_step': 0.2054794579744339
            },
            {
                'latitude': 59.43760681152344,
                'point_count': 1756,
                'longitude_origin': 0.0,
                'longitude_step': 0.20501138269901276
            },
            {
                'latitude': 59.3673095703125,
                'point_count': 1760,
                'longitude_origin': 0.0,
                'longitude_step': 0.20454545319080353
            },
            {
                'latitude': 59.29701232910156,
                'point_count': 1764,
                'longitude_origin': 0.0,
                'longitude_step': 0.20408163964748383
            },
            {
                'latitude': 59.22671127319336,
                'point_count': 1768,
                'longitude_origin': 0.0,
                'longitude_step': 0.20361991226673126
            },
            {
                'latitude': 59.15641403198242,
                'point_count': 1772,
                'longitude_origin': 0.0,
                'longitude_step': 0.20316027104854584
            },
            {
                'latitude': 59.08611297607422,
                'point_count': 1776,
                'longitude_origin': 0.0,
                'longitude_step': 0.20270270109176636
            },
            {
                'latitude': 59.01581573486328,
                'point_count': 1780,
                'longitude_origin': 0.0,
                'longitude_step': 0.20224718749523163
            },
            {
                'latitude': 58.945518493652344,
                'point_count': 1784,
                'longitude_origin': 0.0,
                'longitude_step': 0.20179371535778046
            },
            {
                'latitude': 58.87521743774414,
                'point_count': 1788,
                'longitude_origin': 0.0,
                'longitude_step': 0.20134228467941284
            },
            {
                'latitude': 58.8049201965332,
                'point_count': 1792,
                'longitude_origin': 0.0,
                'longitude_step': 0.2008928507566452
            },
            {
                'latitude': 58.734619140625,
                'point_count': 1796,
                'longitude_origin': 0.0,
                'longitude_step': 0.20044542849063873
            },
            {
                'latitude': 58.66432189941406,
                'point_count': 1800,
                'longitude_origin': 0.0,
                'longitude_step': 0.20000000298023224
            },
            {
                'latitude': 58.594024658203125,
                'point_count': 1804,
                'longitude_origin': 0.0,
                'longitude_step': 0.19955654442310333
            },
            {
                'latitude': 58.52372360229492,
                'point_count': 1808,
                'longitude_origin': 0.0,
                'longitude_step': 0.19911503791809082
            },
            {
                'latitude': 58.453426361083984,
                'point_count': 1812,
                'longitude_origin': 0.0,
                'longitude_step': 0.1986754983663559
            },
            {
                'latitude': 58.38312530517578,
                'point_count': 1816,
                'longitude_origin': 0.0,
                'longitude_step': 0.19823788106441498
            },
            {
                'latitude': 58.312828063964844,
                'point_count': 1820,
                'longitude_origin': 0.0,
                'longitude_step': 0.19780220091342926
            },
            {
                'latitude': 58.242530822753906,
                'point_count': 1824,
                'longitude_origin': 0.0,
                'longitude_step': 0.19736842811107635
            },
            {
                'latitude': 58.1722297668457,
                'point_count': 1828,
                'longitude_origin': 0.0,
                'longitude_step': 0.19693654775619507
            },
            {
                'latitude': 58.101932525634766,
                'point_count': 1832,
                'longitude_origin': 0.0,
                'longitude_step': 0.1965065449476242
            },
            {
                'latitude': 58.03163146972656,
                'point_count': 1836,
                'longitude_origin': 0.0,
                'longitude_step': 0.19607843458652496
            },
            {
                'latitude': 57.961334228515625,
                'point_count': 1840,
                'longitude_origin': 0.0,
                'longitude_step': 0.19565217196941376
            },
            {
                'latitude': 57.89103698730469,
                'point_count': 1844,
                'longitude_origin': 0.0,
                'longitude_step': 0.19522777199745178
            },
            {
                'latitude': 57.820735931396484,
                'point_count': 1848,
                'longitude_origin': 0.0,
                'longitude_step': 0.19480518996715546
            },
            {
                'latitude': 57.75043869018555,
                'point_count': 1852,
                'longitude_origin': 0.0,
                'longitude_step': 0.19438445568084717
            },
            {
                'latitude': 57.680137634277344,
                'point_count': 1856,
                'longitude_origin': 0.0,
                'longitude_step': 0.19396552443504333
            },
            {
                'latitude': 57.609840393066406,
                'point_count': 1860,
                'longitude_origin': 0.0,
                'longitude_step': 0.19354838132858276
            },
            {
                'latitude': 57.53954315185547,
                'point_count': 1864,
                'longitude_origin': 0.0,
                'longitude_step': 0.19313304126262665
            },
            {
                'latitude': 57.469242095947266,
                'point_count': 1868,
                'longitude_origin': 0.0,
                'longitude_step': 0.1927194893360138
            },
            {
                'latitude': 57.39894485473633,
                'point_count': 1872,
                'longitude_origin': 0.0,
                'longitude_step': 0.19230769574642181
            },
            {
                'latitude': 57.328643798828125,
                'point_count': 1876,
                'longitude_origin': 0.0,
                'longitude_step': 0.1918976604938507
            },
            {
                'latitude': 57.25834655761719,
                'point_count': 1880,
                'longitude_origin': 0.0,
                'longitude_step': 0.19148936867713928
            },
            {
                'latitude': 57.18804931640625,
                'point_count': 1884,
                'longitude_origin': 0.0,
                'longitude_step': 0.19108280539512634
            },
            {
                'latitude': 57.11774826049805,
                'point_count': 1888,
                'longitude_origin': 0.0,
                'longitude_step': 0.1906779706478119
            },
            {
                'latitude': 57.04745101928711,
                'point_count': 1892,
                'longitude_origin': 0.0,
                'longitude_step': 0.19027483463287354
            },
            {
                'latitude': 56.977149963378906,
                'point_count': 1896,
                'longitude_origin': 0.0,
                'longitude_step': 0.18987341225147247
            },
            {
                'latitude': 56.90685272216797,
                'point_count': 1900,
                'longitude_origin': 0.0,
                'longitude_step': 0.1894736886024475
            },
            {
                'latitude': 56.83655548095703,
                'point_count': 1904,
                'longitude_origin': 0.0,
                'longitude_step': 0.18907563388347626
            },
            {
                'latitude': 56.76625442504883,
                'point_count': 1908,
                'longitude_origin': 0.0,
                'longitude_step': 0.18867924809455872
            },
            {
                'latitude': 56.69595718383789,
                'point_count': 1912,
                'longitude_origin': 0.0,
                'longitude_step': 0.1882845163345337
            },
            {
                'latitude': 56.62565612792969,
                'point_count': 1916,
                'longitude_origin': 0.0,
                'longitude_step': 0.18789143860340118
            },
            {
                'latitude': 56.55535888671875,
                'point_count': 1920,
                'longitude_origin': 0.0,
                'longitude_step': 0.1875
            },
            {
                'latitude': 56.48506164550781,
                'point_count': 1924,
                'longitude_origin': 0.0,
                'longitude_step': 0.18711018562316895
            },
            {
                'latitude': 56.41476058959961,
                'point_count': 1928,
                'longitude_origin': 0.0,
                'longitude_step': 0.18672199547290802
            },
            {
                'latitude': 56.34446334838867,
                'point_count': 1932,
                'longitude_origin': 0.0,
                'longitude_step': 0.18633539974689484
            },
            {
                'latitude': 56.27416229248047,
                'point_count': 1936,
                'longitude_origin': 0.0,
                'longitude_step': 0.1859504133462906
            },
            {
                'latitude': 56.20386505126953,
                'point_count': 1940,
                'longitude_origin': 0.0,
                'longitude_step': 0.1855670064687729
            },
            {
                'latitude': 56.133567810058594,
                'point_count': 1944,
                'longitude_origin': 0.0,
                'longitude_step': 0.18518517911434174
            },
            {
                'latitude': 56.06326675415039,
                'point_count': 1948,
                'longitude_origin': 0.0,
                'longitude_step': 0.18480493128299713
            },
            {
                'latitude': 55.99296951293945,
                'point_count': 1952,
                'longitude_origin': 0.0,
                'longitude_step': 0.1844262331724167
            },
            {
                'latitude': 55.92266845703125,
                'point_count': 1956,
                'longitude_origin': 0.0,
                'longitude_step': 0.1840490847826004
            },
            {
                'latitude': 55.85237121582031,
                'point_count': 1960,
                'longitude_origin': 0.0,
                'longitude_step': 0.18367347121238708
            },
            {
                'latitude': 55.782073974609375,
                'point_count': 1964,
                'longitude_origin': 0.0,
                'longitude_step': 0.18329939246177673
            },
            {
                'latitude': 55.71177291870117,
                'point_count': 1968,
                'longitude_origin': 0.0,
                'longitude_step': 0.18292683362960815
            },
            {
                'latitude': 55.641475677490234,
                'point_count': 1972,
                'longitude_origin': 0.0,
                'longitude_step': 0.18255577981472015
            },
            {
                'latitude': 55.57117462158203,
                'point_count': 1976,
                'longitude_origin': 0.0,
                'longitude_step': 0.18218623101711273
            },
            {
                'latitude': 55.500877380371094,
                'point_count': 1980,
                'longitude_origin': 0.0,
                'longitude_step': 0.1818181872367859
            },
            {
                'latitude': 55.430580139160156,
                'point_count': 1984,
                'longitude_origin': 0.0,
                'longitude_step': 0.18145161867141724
            },
            {
                'latitude': 55.36027908325195,
                'point_count': 1988,
                'longitude_origin': 0.0,
                'longitude_step': 0.18108652532100677
            },
            {
                'latitude': 55.289981842041016,
                'point_count': 1992,
                'longitude_origin': 0.0,
                'longitude_step': 0.1807228922843933
            },
            {
                'latitude': 55.21968078613281,
                'point_count': 1996,
                'longitude_origin': 0.0,
                'longitude_step': 0.18036071956157684
            },
            {
                'latitude': 55.149383544921875,
                'point_count': 2000,
                'longitude_origin': 0.0,
                'longitude_step': 0.18000000715255737
            },
            {
                'latitude': 55.07908630371094,
                'point_count': 2004,
                'longitude_origin': 0.0,
                'longitude_step': 0.1796407252550125
            },
            {
                'latitude': 55.008785247802734,
                'point_count': 2008,
                'longitude_origin': 0.0,
                'longitude_step': 0.17928287386894226
            },
            {
                'latitude': 54.9384880065918,
                'point_count': 2012,
                'longitude_origin': 0.0,
                'longitude_step': 0.17892643809318542
            },
            {
                'latitude': 54.868186950683594,
                'point_count': 2016,
                'longitude_origin': 0.0,
                'longitude_step': 0.1785714328289032
            },
            {
                'latitude': 54.797889709472656,
                'point_count': 2020,
                'longitude_origin': 0.0,
                'longitude_step': 0.1782178282737732
            },
            {
                'latitude': 54.72759246826172,
                'point_count': 2024,
                'longitude_origin': 0.0,
                'longitude_step': 0.17786560952663422
            },
            {
                'latitude': 54.657291412353516,
                'point_count': 2028,
                'longitude_origin': 0.0,
                'longitude_step': 0.17751479148864746
            },
            {
                'latitude': 54.58699417114258,
                'point_count': 2032,
                'longitude_origin': 0.0,
                'longitude_step': 0.17716535925865173
            },
            {
                'latitude': 54.516693115234375,
                'point_count': 2036,
                'longitude_origin': 0.0,
                'longitude_step': 0.17681728303432465
            },
            {
                'latitude': 54.44639587402344,
                'point_count': 2040,
                'longitude_origin': 0.0,
                'longitude_step': 0.1764705926179886
            },
            {
                'latitude': 54.3760986328125,
                'point_count': 2044,
                'longitude_origin': 0.0,
                'longitude_step': 0.17612524330615997
            },
            {
                'latitude': 54.3057975769043,
                'point_count': 2048,
                'longitude_origin': 0.0,
                'longitude_step': 0.17578125
            },
            {
                'latitude': 54.23550033569336,
                'point_count': 2052,
                'longitude_origin': 0.0,
                'longitude_step': 0.17543859779834747
            },
            {
                'latitude': 54.165199279785156,
                'point_count': 2056,
                'longitude_origin': 0.0,
                'longitude_step': 0.1750972718000412
            },
            {
                'latitude': 54.09490203857422,
                'point_count': 2060,
                'longitude_origin': 0.0,
                'longitude_step': 0.17475728690624237
            },
            {
                'latitude': 54.02460479736328,
                'point_count': 2064,
                'longitude_origin': 0.0,
                'longitude_step': 0.1744185984134674
            },
            {
                'latitude': 53.95430374145508,
                'point_count': 2068,
                'longitude_origin': 0.0,
                'longitude_step': 0.1740812361240387
            },
            {
                'latitude': 53.88400650024414,
                'point_count': 2072,
                'longitude_origin': 0.0,
                'longitude_step': 0.17374517023563385
            },
            {
                'latitude': 53.81370544433594,
                'point_count': 2076,
                'longitude_origin': 0.0,
                'longitude_step': 0.17341040074825287
            },
            {
                'latitude': 53.743408203125,
                'point_count': 2080,
                'longitude_origin': 0.0,
                'longitude_step': 0.17307692766189575
            },
            {
                'latitude': 53.6731071472168,
                'point_count': 2084,
                'longitude_origin': 0.0,
                'longitude_step': 0.1727447211742401
            },
            {
                'latitude': 53.60280990600586,
                'point_count': 2088,
                'longitude_origin': 0.0,
                'longitude_step': 0.17241379618644714
            },
            {
                'latitude': 53.53251266479492,
                'point_count': 2092,
                'longitude_origin': 0.0,
                'longitude_step': 0.17208412289619446
            },
            {
                'latitude': 53.46221160888672,
                'point_count': 2096,
                'longitude_origin': 0.0,
                'longitude_step': 0.17175573110580444
            },
            {
                'latitude': 53.39191436767578,
                'point_count': 2100,
                'longitude_origin': 0.0,
                'longitude_step': 0.17142857611179352
            },
            {
                'latitude': 53.32161331176758,
                'point_count': 2104,
                'longitude_origin': 0.0,
                'longitude_step': 0.17110265791416168
            },
            {
                'latitude': 53.25131607055664,
                'point_count': 2108,
                'longitude_origin': 0.0,
                'longitude_step': 0.17077799141407013
            },
            {
                'latitude': 53.1810188293457,
                'point_count': 2112,
                'longitude_origin': 0.0,
                'longitude_step': 0.17045454680919647
            },
            {
                'latitude': 53.1107177734375,
                'point_count': 2116,
                'longitude_origin': 0.0,
                'longitude_step': 0.1701323240995407
            },
            {
                'latitude': 53.04042053222656,
                'point_count': 2120,
                'longitude_origin': 0.0,
                'longitude_step': 0.16981132328510284
            },
            {
                'latitude': 52.97011947631836,
                'point_count': 2124,
                'longitude_origin': 0.0,
                'longitude_step': 0.16949152946472168
            },
            {
                'latitude': 52.89982223510742,
                'point_count': 2128,
                'longitude_origin': 0.0,
                'longitude_step': 0.16917292773723602
            },
            {
                'latitude': 52.829524993896484,
                'point_count': 2132,
                'longitude_origin': 0.0,
                'longitude_step': 0.16885553300380707
            },
            {
                'latitude': 52.75922393798828,
                'point_count': 2136,
                'longitude_origin': 0.0,
                'longitude_step': 0.16853933036327362
            },
            {
                'latitude': 52.688926696777344,
                'point_count': 2140,
                'longitude_origin': 0.0,
                'longitude_step': 0.1682243049144745
            },
            {
                'latitude': 52.61862564086914,
                'point_count': 2144,
                'longitude_origin': 0.0,
                'longitude_step': 0.16791044175624847
            },
            {
                'latitude': 52.5483283996582,
                'point_count': 2148,
                'longitude_origin': 0.0,
                'longitude_step': 0.16759777069091797
            },
            {
                'latitude': 52.478031158447266,
                'point_count': 2152,
                'longitude_origin': 0.0,
                'longitude_step': 0.1672862470149994
            },
            {
                'latitude': 52.40773010253906,
                'point_count': 2156,
                'longitude_origin': 0.0,
                'longitude_step': 0.16697588562965393
            },
            {
                'latitude': 52.337432861328125,
                'point_count': 2160,
                'longitude_origin': 0.0,
                'longitude_step': 0.1666666716337204
            },
            {
                'latitude': 52.26713180541992,
                'point_count': 2164,
                'longitude_origin': 0.0,
                'longitude_step': 0.1663585901260376
            },
            {
                'latitude': 52.196834564208984,
                'point_count': 2168,
                'longitude_origin': 0.0,
                'longitude_step': 0.16605165600776672
            },
            {
                'latitude': 52.12653732299805,
                'point_count': 2172,
                'longitude_origin': 0.0,
                'longitude_step': 0.16574585437774658
            },
            {
                'latitude': 52.056236267089844,
                'point_count': 2176,
                'longitude_origin': 0.0,
                'longitude_step': 0.16544117033481598
            },
            {
                'latitude': 51.985939025878906,
                'point_count': 2180,
                'longitude_origin': 0.0,
                'longitude_step': 0.1651376187801361
            },
            {
                'latitude': 51.9156379699707,
                'point_count': 2184,
                'longitude_origin': 0.0,
                'longitude_step': 0.16483516991138458
            },
            {
                'latitude': 51.845340728759766,
                'point_count': 2188,
                'longitude_origin': 0.0,
                'longitude_step': 0.1645338237285614
            },
            {
                'latitude': 51.77504348754883,
                'point_count': 2192,
                'longitude_origin': 0.0,
                'longitude_step': 0.16423358023166656
            },
            {
                'latitude': 51.704742431640625,
                'point_count': 2196,
                'longitude_origin': 0.0,
                'longitude_step': 0.16393442451953888
            },
            {
                'latitude': 51.63444519042969,
                'point_count': 2200,
                'longitude_origin': 0.0,
                'longitude_step': 0.16363635659217834
            },
            {
                'latitude': 51.564144134521484,
                'point_count': 2204,
                'longitude_origin': 0.0,
                'longitude_step': 0.16333937644958496
            },
            {
                'latitude': 51.49384689331055,
                'point_count': 2208,
                'longitude_origin': 0.0,
                'longitude_step': 0.16304348409175873
            },
            {
                'latitude': 51.42354965209961,
                'point_count': 2212,
                'longitude_origin': 0.0,
                'longitude_step': 0.16274864971637726
            },
            {
                'latitude': 51.353248596191406,
                'point_count': 2216,
                'longitude_origin': 0.0,
                'longitude_step': 0.16245487332344055
            },
            {
                'latitude': 51.28295135498047,
                'point_count': 2220,
                'longitude_origin': 0.0,
                'longitude_step': 0.1621621549129486
            },
            {
                'latitude': 51.212650299072266,
                'point_count': 2224,
                'longitude_origin': 0.0,
                'longitude_step': 0.16187050938606262
            },
            {
                'latitude': 51.14235305786133,
                'point_count': 2228,
                'longitude_origin': 0.0,
                'longitude_step': 0.161579892039299
            },
            {
                'latitude': 51.07205581665039,
                'point_count': 2232,
                'longitude_origin': 0.0,
                'longitude_step': 0.16129031777381897
            },
            {
                'latitude': 51.00175476074219,
                'point_count': 2236,
                'longitude_origin': 0.0,
                'longitude_step': 0.1610017865896225
            },
            {
                'latitude': 50.93145751953125,
                'point_count': 2240,
                'longitude_origin': 0.0,
                'longitude_step': 0.1607142835855484
            },
            {
                'latitude': 50.86115646362305,
                'point_count': 2244,
                'longitude_origin': 0.0,
                'longitude_step': 0.16042780876159668
            },
            {
                'latitude': 50.79085922241211,
                'point_count': 2248,
                'longitude_origin': 0.0,
                'longitude_step': 0.16014234721660614
            },
            {
                'latitude': 50.72056198120117,
                'point_count': 2252,
                'longitude_origin': 0.0,
                'longitude_step': 0.15985789895057678
            },
            {
                'latitude': 50.65026092529297,
                'point_count': 2256,
                'longitude_origin': 0.0,
                'longitude_step': 0.1595744639635086
            },
            {
                'latitude': 50.57996368408203,
                'point_count': 2260,
                'longitude_origin': 0.0,
                'longitude_step': 0.1592920422554016
            },
            {
                'latitude': 50.50966262817383,
                'point_count': 2264,
                'longitude_origin': 0.0,
                'longitude_step': 0.1590106040239334
            },
            {
                'latitude': 50.43936538696289,
                'point_count': 2268,
                'longitude_origin': 0.0,
                'longitude_step': 0.1587301641702652
            },
            {
                'latitude': 50.36906814575195,
                'point_count': 2272,
                'longitude_origin': 0.0,
                'longitude_step': 0.15845070779323578
            },
            {
                'latitude': 50.29876708984375,
                'point_count': 2276,
                'longitude_origin': 0.0,
                'longitude_step': 0.15817223489284515
            },
            {
                'latitude': 50.22846984863281,
                'point_count': 2280,
                'longitude_origin': 0.0,
                'longitude_step': 0.15789473056793213
            },
            {
                'latitude': 50.15816879272461,
                'point_count': 2284,
                'longitude_origin': 0.0,
                'longitude_step': 0.1576182097196579
            },
            {
                'latitude': 50.08787155151367,
                'point_count': 2288,
                'longitude_origin': 0.0,
                'longitude_step': 0.15734265744686127
            },
            {
                'latitude': 50.017574310302734,
                'point_count': 2292,
                'longitude_origin': 0.0,
                'longitude_step': 0.15706805884838104
            },
            {
                'latitude': 49.94727325439453,
                'point_count': 2296,
                'longitude_origin': 0.0,
                'longitude_step': 0.15679442882537842
            },
            {
                'latitude': 49.876976013183594,
                'point_count': 2300,
                'longitude_origin': 0.0,
                'longitude_step': 0.156521737575531
            },
            {
                'latitude': 49.80667495727539,
                'point_count': 2304,
                'longitude_origin': 0.0,
                'longitude_step': 0.15625
            },
            {
                'latitude': 49.73637771606445,
                'point_count': 2308,
                'longitude_origin': 0.0,
                'longitude_step': 0.1559792011976242
            },
            {
                'latitude': 49.666080474853516,
                'point_count': 2312,
                'longitude_origin': 0.0,
                'longitude_step': 0.15570934116840363
            },
            {
                'latitude': 49.59577941894531,
                'point_count': 2316,
                'longitude_origin': 0.0,
                'longitude_step': 0.15544041991233826
            },
            {
                'latitude': 49.525482177734375,
                'point_count': 2320,
                'longitude_origin': 0.0,
                'longitude_step': 0.1551724076271057
            },
            {
                'latitude': 49.45518112182617,
                'point_count': 2324,
                'longitude_origin': 0.0,
                'longitude_step': 0.15490533411502838
            },
            {
                'latitude': 49.384883880615234,
                'point_count': 2328,
                'longitude_origin': 0.0,
                'longitude_step': 0.15463916957378387
            },
            {
                'latitude': 49.3145866394043,
                'point_count': 2332,
                'longitude_origin': 0.0,
                'longitude_step': 0.1543739289045334
            },
            {
                'latitude': 49.244285583496094,
                'point_count': 2336,
                'longitude_origin': 0.0,
                'longitude_step': 0.15410958230495453
            },
            {
                'latitude': 49.173988342285156,
                'point_count': 2340,
                'longitude_origin': 0.0,
                'longitude_step': 0.1538461595773697
            },
            {
                'latitude': 49.10368728637695,
                'point_count': 2344,
                'longitude_origin': 0.0,
                'longitude_step': 0.1535836160182953
            },
            {
                'latitude': 49.033390045166016,
                'point_count': 2348,
                'longitude_origin': 0.0,
                'longitude_step': 0.1533219814300537
            },
            {
                'latitude': 48.96309280395508,
                'point_count': 2352,
                'longitude_origin': 0.0,
                'longitude_step': 0.15306122601032257
            },
            {
                'latitude': 48.892791748046875,
                'point_count': 2356,
                'longitude_origin': 0.0,
                'longitude_step': 0.15280136466026306
            },
            {
                'latitude': 48.82249450683594,
                'point_count': 2360,
                'longitude_origin': 0.0,
                'longitude_step': 0.1525423675775528
            },
            {
                'latitude': 48.752193450927734,
                'point_count': 2364,
                'longitude_origin': 0.0,
                'longitude_step': 0.15228426456451416
            },
            {
                'latitude': 48.6818962097168,
                'point_count': 2368,
                'longitude_origin': 0.0,
                'longitude_step': 0.15202702581882477
            },
            {
                'latitude': 48.61159896850586,
                'point_count': 2372,
                'longitude_origin': 0.0,
                'longitude_step': 0.15177065134048462
            },
            {
                'latitude': 48.541297912597656,
                'point_count': 2376,
                'longitude_origin': 0.0,
                'longitude_step': 0.1515151560306549
            },
            {
                'latitude': 48.47100067138672,
                'point_count': 2380,
                'longitude_origin': 0.0,
                'longitude_step': 0.15126051008701324
            },
            {
                'latitude': 48.400699615478516,
                'point_count': 2384,
                'longitude_origin': 0.0,
                'longitude_step': 0.15100671350955963
            },
            {
                'latitude': 48.33040237426758,
                'point_count': 2388,
                'longitude_origin': 0.0,
                'longitude_step': 0.15075376629829407
            },
            {
                'latitude': 48.26010513305664,
                'point_count': 2392,
                'longitude_origin': 0.0,
                'longitude_step': 0.15050166845321655
            },
            {
                'latitude': 48.18980407714844,
                'point_count': 2396,
                'longitude_origin': 0.0,
                'longitude_step': 0.1502504199743271
            },
            {
                'latitude': 48.1195068359375,
                'point_count': 2400,
                'longitude_origin': 0.0,
                'longitude_step': 0.15000000596046448
            },
            {
                'latitude': 48.0492057800293,
                'point_count': 2404,
                'longitude_origin': 0.0,
                'longitude_step': 0.14975041151046753
            },
            {
                'latitude': 47.97890853881836,
                'point_count': 2408,
                'longitude_origin': 0.0,
                'longitude_step': 0.14950166642665863
            },
            {
                'latitude': 47.90861129760742,
                'point_count': 2412,
                'longitude_origin': 0.0,
                'longitude_step': 0.1492537260055542
            },
            {
                'latitude': 47.83831024169922,
                'point_count': 2416,
                'longitude_origin': 0.0,
                'longitude_step': 0.14900662004947662
            },
            {
                'latitude': 47.76801300048828,
                'point_count': 2420,
                'longitude_origin': 0.0,
                'longitude_step': 0.1487603336572647
            },
            {
                'latitude': 47.69771194458008,
                'point_count': 2424,
                'longitude_origin': 0.0,
                'longitude_step': 0.14851485192775726
            },
            {
                'latitude': 47.62741470336914,
                'point_count': 2428,
                'longitude_origin': 0.0,
                'longitude_step': 0.14827017486095428
            },
            {
                'latitude': 47.5571174621582,
                'point_count': 2432,
                'longitude_origin': 0.0,
                'longitude_step': 0.14802631735801697
            },
            {
                'latitude': 47.48681640625,
                'point_count': 2436,
                'longitude_origin': 0.0,
                'longitude_step': 0.14778324961662292
            },
            {
                'latitude': 47.41651916503906,
                'point_count': 2440,
                'longitude_origin': 0.0,
                'longitude_step': 0.14754098653793335
            },
            {
                'latitude': 47.34621810913086,
                'point_count': 2444,
                'longitude_origin': 0.0,
                'longitude_step': 0.14729951322078705
            },
            {
                'latitude': 47.27592086791992,
                'point_count': 2448,
                'longitude_origin': 0.0,
                'longitude_step': 0.14705882966518402
            },
            {
                'latitude': 47.205623626708984,
                'point_count': 2452,
                'longitude_origin': 0.0,
                'longitude_step': 0.14681892096996307
            },
            {
                'latitude': 47.13532257080078,
                'point_count': 2456,
                'longitude_origin': 0.0,
                'longitude_step': 0.1465798020362854
            },
            {
                'latitude': 47.065025329589844,
                'point_count': 2460,
                'longitude_origin': 0.0,
                'longitude_step': 0.1463414579629898
            },
            {
                'latitude': 46.99472427368164,
                'point_count': 2464,
                'longitude_origin': 0.0,
                'longitude_step': 0.1461038887500763
            },
            {
                'latitude': 46.9244270324707,
                'point_count': 2468,
                'longitude_origin': 0.0,
                'longitude_step': 0.14586709439754486
            },
            {
                'latitude': 46.854129791259766,
                'point_count': 2472,
                'longitude_origin': 0.0,
                'longitude_step': 0.1456310749053955
            },
            {
                'latitude': 46.78382873535156,
                'point_count': 2476,
                'longitude_origin': 0.0,
                'longitude_step': 0.14539580047130585
            },
            {
                'latitude': 46.713531494140625,
                'point_count': 2480,
                'longitude_origin': 0.0,
                'longitude_step': 0.14516128599643707
            },
            {
                'latitude': 46.64323043823242,
                'point_count': 2484,
                'longitude_origin': 0.0,
                'longitude_step': 0.14492753148078918
            },
            {
                'latitude': 46.572933197021484,
                'point_count': 2488,
                'longitude_origin': 0.0,
                'longitude_step': 0.14469453692436218
            },
            {
                'latitude': 46.50263595581055,
                'point_count': 2492,
                'longitude_origin': 0.0,
                'longitude_step': 0.14446227252483368
            },
            {
                'latitude': 46.432334899902344,
                'point_count': 2496,
                'longitude_origin': 0.0,
                'longitude_step': 0.14423076808452606
            },
            {
                'latitude': 46.362037658691406,
                'point_count': 2500,
                'longitude_origin': 0.0,
                'longitude_step': 0.14399999380111694
            },
            {
                'latitude': 46.2917366027832,
                'point_count': 2504,
                'longitude_origin': 0.0,
                'longitude_step': 0.14376996457576752
            },
            {
                'latitude': 46.221439361572266,
                'point_count': 2508,
                'longitude_origin': 0.0,
                'longitude_step': 0.1435406655073166
            },
            {
                'latitude': 46.15114212036133,
                'point_count': 2512,
                'longitude_origin': 0.0,
                'longitude_step': 0.14331209659576416
            },
            {
                'latitude': 46.080841064453125,
                'point_count': 2516,
                'longitude_origin': 0.0,
                'longitude_step': 0.14308425784111023
            },
            {
                'latitude': 46.01054382324219,
                'point_count': 2520,
                'longitude_origin': 0.0,
                'longitude_step': 0.1428571492433548
            },
            {
                'latitude': 45.940242767333984,
                'point_count': 2524,
                'longitude_origin': 0.0,
                'longitude_step': 0.14263074100017548
            },
            {
                'latitude': 45.86994552612305,
                'point_count': 2528,
                'longitude_origin': 0.0,
                'longitude_step': 0.14240506291389465
            },
            {
                'latitude': 45.79964828491211,
                'point_count': 2532,
                'longitude_origin': 0.0,
                'longitude_step': 0.14218010008335114
            },
            {
                'latitude': 45.729347229003906,
                'point_count': 2536,
                'longitude_origin': 0.0,
                'longitude_step': 0.14195583760738373
            },
            {
                'latitude': 45.65904998779297,
                'point_count': 2540,
                'longitude_origin': 0.0,
                'longitude_step': 0.14173229038715363
            },
            {
                'latitude': 45.588748931884766,
                'point_count': 2544,
                'longitude_origin': 0.0,
                'longitude_step': 0.14150942862033844
            },
            {
                'latitude': 45.51845169067383,
                'point_count': 2548,
                'longitude_origin': 0.0,
                'longitude_step': 0.14128728210926056
            },
            {
                'latitude': 45.44815444946289,
                'point_count': 2552,
                'longitude_origin': 0.0,
                'longitude_step': 0.1410658359527588
            },
            {
                'latitude': 45.37785339355469,
                'point_count': 2556,
                'longitude_origin': 0.0,
                'longitude_step': 0.14084507524967194
            },
            {
                'latitude': 45.30755615234375,
                'point_count': 2560,
                'longitude_origin': 0.0,
                'longitude_step': 0.140625
            },
            {
                'latitude': 45.23725509643555,
                'point_count': 2564,
                'longitude_origin': 0.0,
                'longitude_step': 0.14040561020374298
            },
            {
                'latitude': 45.16695785522461,
                'point_count': 2568,
                'longitude_origin': 0.0,
                'longitude_step': 0.14018692076206207
            },
            {
                'latitude': 45.09666061401367,
                'point_count': 2572,
                'longitude_origin': 0.0,
                'longitude_step': 0.1399689018726349
            },
            {
                'latitude': 45.02635955810547,
                'point_count': 2576,
                'longitude_origin': 0.0,
                'longitude_step': 0.13975155353546143
            },
            {
                'latitude': 44.95606231689453,
                'point_count': 2580,
                'longitude_origin': 0.0,
                'longitude_step': 0.13953489065170288
            },
            {
                'latitude': 44.88576126098633,
                'point_count': 2584,
                'longitude_origin': 0.0,
                'longitude_step': 0.13931888341903687
            },
            {
                'latitude': 44.81546401977539,
                'point_count': 2588,
                'longitude_origin': 0.0,
                'longitude_step': 0.13910356163978577
            },
            {
                'latitude': 44.74516677856445,
                'point_count': 2592,
                'longitude_origin': 0.0,
                'longitude_step': 0.1388888955116272
            },
            {
                'latitude': 44.67486572265625,
                'point_count': 2596,
                'longitude_origin': 0.0,
                'longitude_step': 0.13867488503456116
            },
            {
                'latitude': 44.60456848144531,
                'point_count': 2600,
                'longitude_origin': 0.0,
                'longitude_step': 0.13846154510974884
            },
            {
                'latitude': 44.53426742553711,
                'point_count': 2604,
                'longitude_origin': 0.0,
                'longitude_step': 0.13824884593486786
            },
            {
                'latitude': 44.46397018432617,
                'point_count': 2608,
                'longitude_origin': 0.0,
                'longitude_step': 0.1380368024110794
            },
            {
                'latitude': 44.393672943115234,
                'point_count': 2612,
                'longitude_origin': 0.0,
                'longitude_step': 0.13782541453838348
            },
            {
                'latitude': 44.32337188720703,
                'point_count': 2616,
                'longitude_origin': 0.0,
                'longitude_step': 0.1376146823167801
            },
            {
                'latitude': 44.253074645996094,
                'point_count': 2620,
                'longitude_origin': 0.0,
                'longitude_step': 0.13740457594394684
            },
            {
                'latitude': 44.18277359008789,
                'point_count': 2624,
                'longitude_origin': 0.0,
                'longitude_step': 0.13719512522220612
            },
            {
                'latitude': 44.11247634887695,
                'point_count': 2628,
                'longitude_origin': 0.0,
                'longitude_step': 0.13698630034923553
            },
            {
                'latitude': 44.042179107666016,
                'point_count': 2632,
                'longitude_origin': 0.0,
                'longitude_step': 0.1367781162261963
            },
            {
                'latitude': 43.97187805175781,
                'point_count': 2636,
                'longitude_origin': 0.0,
                'longitude_step': 0.13657055795192719
            },
            {
                'latitude': 43.901580810546875,
                'point_count': 2640,
                'longitude_origin': 0.0,
                'longitude_step': 0.13636364042758942
            },
            {
                'latitude': 43.83127975463867,
                'point_count': 2644,
                'longitude_origin': 0.0,
                'longitude_step': 0.1361573338508606
            },
            {
                'latitude': 43.760982513427734,
                'point_count': 2648,
                'longitude_origin': 0.0,
                'longitude_step': 0.1359516680240631
            },
            {
                'latitude': 43.6906852722168,
                'point_count': 2652,
                'longitude_origin': 0.0,
                'longitude_step': 0.13574661314487457
            },
            {
                'latitude': 43.620384216308594,
                'point_count': 2656,
                'longitude_origin': 0.0,
                'longitude_step': 0.13554216921329498
            },
            {
                'latitude': 43.550086975097656,
                'point_count': 2660,
                'longitude_origin': 0.0,
                'longitude_step': 0.13533835113048553
            },
            {
                'latitude': 43.47978591918945,
                'point_count': 2664,
                'longitude_origin': 0.0,
                'longitude_step': 0.13513512909412384
            },
            {
                'latitude': 43.409488677978516,
                'point_count': 2668,
                'longitude_origin': 0.0,
                'longitude_step': 0.1349325329065323
            },
            {
                'latitude': 43.33919143676758,
                'point_count': 2672,
                'longitude_origin': 0.0,
                'longitude_step': 0.1347305327653885
            },
            {
                'latitude': 43.268890380859375,
                'point_count': 2676,
                'longitude_origin': 0.0,
                'longitude_step': 0.13452914357185364
            },
            {
                'latitude': 43.19859313964844,
                'point_count': 2680,
                'longitude_origin': 0.0,
                'longitude_step': 0.13432836532592773
            },
            {
                'latitude': 43.128292083740234,
                'point_count': 2684,
                'longitude_origin': 0.0,
                'longitude_step': 0.1341281682252884
            },
            {
                'latitude': 43.0579948425293,
                'point_count': 2688,
                'longitude_origin': 0.0,
                'longitude_step': 0.1339285671710968
            },
            {
                'latitude': 42.98769760131836,
                'point_count': 2692,
                'longitude_origin': 0.0,
                'longitude_step': 0.13372956216335297
            },
            {
                'latitude': 42.917396545410156,
                'point_count': 2696,
                'longitude_origin': 0.0,
                'longitude_step': 0.13353115320205688
            },
            {
                'latitude': 42.84709930419922,
                'point_count': 2700,
                'longitude_origin': 0.0,
                'longitude_step': 0.13333334028720856
            },
            {
                'latitude': 42.776798248291016,
                'point_count': 2704,
                'longitude_origin': 0.0,
                'longitude_step': 0.1331360936164856
            },
            {
                'latitude': 42.70650100708008,
                'point_count': 2708,
                'longitude_origin': 0.0,
                'longitude_step': 0.1329394429922104
            },
            {
                'latitude': 42.63620376586914,
                'point_count': 2712,
                'longitude_origin': 0.0,
                'longitude_step': 0.13274335861206055
            },
            {
                'latitude': 42.56590270996094,
                'point_count': 2716,
                'longitude_origin': 0.0,
                'longitude_step': 0.13254787027835846
            },
            {
                'latitude': 42.49560546875,
                'point_count': 2720,
                'longitude_origin': 0.0,
                'longitude_step': 0.13235294818878174
            },
            {
                'latitude': 42.4253044128418,
                'point_count': 2724,
                'longitude_origin': 0.0,
                'longitude_step': 0.13215859234333038
            },
            {
                'latitude': 42.35500717163086,
                'point_count': 2728,
                'longitude_origin': 0.0,
                'longitude_step': 0.1319648027420044
            },
            {
                'latitude': 42.28470993041992,
                'point_count': 2732,
                'longitude_origin': 0.0,
                'longitude_step': 0.13177159428596497
            },
            {
                'latitude': 42.21440887451172,
                'point_count': 2736,
                'longitude_origin': 0.0,
                'longitude_step': 0.1315789520740509
            },
            {
                'latitude': 42.14411163330078,
                'point_count': 2740,
                'longitude_origin': 0.0,
                'longitude_step': 0.131386861205101
            },
            {
                'latitude': 42.07381057739258,
                'point_count': 2744,
                'longitude_origin': 0.0,
                'longitude_step': 0.1311953365802765
            },
            {
                'latitude': 42.00351333618164,
                'point_count': 2748,
                'longitude_origin': 0.0,
                'longitude_step': 0.13100436329841614
            },
            {
                'latitude': 41.9332160949707,
                'point_count': 2752,
                'longitude_origin': 0.0,
                'longitude_step': 0.13081395626068115
            },
            {
                'latitude': 41.8629150390625,
                'point_count': 2756,
                'longitude_origin': 0.0,
                'longitude_step': 0.13062408566474915
            },
            {
                'latitude': 41.79261779785156,
                'point_count': 2760,
                'longitude_origin': 0.0,
                'longitude_step': 0.1304347813129425
            },
            {
                'latitude': 41.72231674194336,
                'point_count': 2764,
                'longitude_origin': 0.0,
                'longitude_step': 0.13024601340293884
            },
            {
                'latitude': 41.65201950073242,
                'point_count': 2768,
                'longitude_origin': 0.0,
                'longitude_step': 0.13005779683589935
            },
            {
                'latitude': 41.581722259521484,
                'point_count': 2772,
                'longitude_origin': 0.0,
                'longitude_step': 0.12987013161182404
            },
            {
                'latitude': 41.51142120361328,
                'point_count': 2776,
                'longitude_origin': 0.0,
                'longitude_step': 0.1296830028295517
            },
            {
                'latitude': 41.441123962402344,
                'point_count': 2780,
                'longitude_origin': 0.0,
                'longitude_step': 0.12949639558792114
            },
            {
                'latitude': 41.37082290649414,
                'point_count': 2784,
                'longitude_origin': 0.0,
                'longitude_step': 0.12931033968925476
            },
            {
                'latitude': 41.3005256652832,
                'point_count': 2788,
                'longitude_origin': 0.0,
                'longitude_step': 0.12912482023239136
            },
            {
                'latitude': 41.230228424072266,
                'point_count': 2792,
                'longitude_origin': 0.0,
                'longitude_step': 0.12893982231616974
            },
            {
                'latitude': 41.15992736816406,
                'point_count': 2796,
                'longitude_origin': 0.0,
                'longitude_step': 0.1287553608417511
            },
            {
                'latitude': 41.089630126953125,
                'point_count': 2800,
                'longitude_origin': 0.0,
                'longitude_step': 0.12857143580913544
            },
            {
                'latitude': 41.01932907104492,
                'point_count': 2804,
                'longitude_origin': 0.0,
                'longitude_step': 0.12838801741600037
            },
            {
                'latitude': 40.949031829833984,
                'point_count': 2808,
                'longitude_origin': 0.0,
                'longitude_step': 0.12820513546466827
            },
            {
                'latitude': 40.87873458862305,
                'point_count': 2812,
                'longitude_origin': 0.0,
                'longitude_step': 0.12802276015281677
            },
            {
                'latitude': 40.808433532714844,
                'point_count': 2816,
                'longitude_origin': 0.0,
                'longitude_step': 0.12784090638160706
            },
            {
                'latitude': 40.738136291503906,
                'point_count': 2820,
                'longitude_origin': 0.0,
                'longitude_step': 0.12765957415103912
            },
            {
                'latitude': 40.6678352355957,
                'point_count': 2824,
                'longitude_origin': 0.0,
                'longitude_step': 0.12747874855995178
            },
            {
                'latitude': 40.597537994384766,
                'point_count': 2828,
                'longitude_origin': 0.0,
                'longitude_step': 0.12729844450950623
            },
            {
                'latitude': 40.52724075317383,
                'point_count': 2832,
                'longitude_origin': 0.0,
                'longitude_step': 0.12711864709854126
            },
            {
                'latitude': 40.456939697265625,
                'point_count': 2836,
                'longitude_origin': 0.0,
                'longitude_step': 0.12693935632705688
            },
            {
                'latitude': 40.38664245605469,
                'point_count': 2840,
                'longitude_origin': 0.0,
                'longitude_step': 0.1267605572938919
            },
            {
                'latitude': 40.316341400146484,
                'point_count': 2844,
                'longitude_origin': 0.0,
                'longitude_step': 0.1265822798013687
            },
            {
                'latitude': 40.24604415893555,
                'point_count': 2848,
                'longitude_origin': 0.0,
                'longitude_step': 0.12640449404716492
            },
            {
                'latitude': 40.17574691772461,
                'point_count': 2852,
                'longitude_origin': 0.0,
                'longitude_step': 0.1262272149324417
            },
            {
                'latitude': 40.105445861816406,
                'point_count': 2856,
                'longitude_origin': 0.0,
                'longitude_step': 0.1260504275560379
            },
            {
                'latitude': 40.03514862060547,
                'point_count': 2860,
                'longitude_origin': 0.0,
                'longitude_step': 0.1258741319179535
            },
            {
                'latitude': 39.964847564697266,
                'point_count': 2864,
                'longitude_origin': 0.0,
                'longitude_step': 0.12569832801818848
            },
            {
                'latitude': 39.89455032348633,
                'point_count': 2868,
                'longitude_origin': 0.0,
                'longitude_step': 0.12552301585674286
            },
            {
                'latitude': 39.82425308227539,
                'point_count': 2872,
                'longitude_origin': 0.0,
                'longitude_step': 0.12534819543361664
            },
            {
                'latitude': 39.75395202636719,
                'point_count': 2876,
                'longitude_origin': 0.0,
                'longitude_step': 0.12517385184764862
            },
            {
                'latitude': 39.68365478515625,
                'point_count': 2880,
                'longitude_origin': 0.0,
                'longitude_step': 0.125
            },
            {
                'latitude': 39.61335372924805,
                'point_count': 2884,
                'longitude_origin': 0.0,
                'longitude_step': 0.12482663244009018
            },
            {
                'latitude': 39.54305648803711,
                'point_count': 2888,
                'longitude_origin': 0.0,
                'longitude_step': 0.12465374171733856
            },
            {
                'latitude': 39.47275924682617,
                'point_count': 2892,
                'longitude_origin': 0.0,
                'longitude_step': 0.12448132783174515
            },
            {
                'latitude': 39.40245819091797,
                'point_count': 2896,
                'longitude_origin': 0.0,
                'longitude_step': 0.12430939078330994
            },
            {
                'latitude': 39.33216094970703,
                'point_count': 2900,
                'longitude_origin': 0.0,
                'longitude_step': 0.12413793057203293
            },
            {
                'latitude': 39.26185989379883,
                'point_count': 2904,
                'longitude_origin': 0.0,
                'longitude_step': 0.12396693974733353
            },
            {
                'latitude': 39.19156265258789,
                'point_count': 2908,
                'longitude_origin': 0.0,
                'longitude_step': 0.12379642575979233
            },
            {
                'latitude': 39.12126541137695,
                'point_count': 2912,
                'longitude_origin': 0.0,
                'longitude_step': 0.12362637370824814
            },
            {
                'latitude': 39.05096435546875,
                'point_count': 2916,
                'longitude_origin': 0.0,
                'longitude_step': 0.12345679104328156
            },
            {
                'latitude': 38.98066711425781,
                'point_count': 2920,
                'longitude_origin': 0.0,
                'longitude_step': 0.12328767031431198
            },
            {
                'latitude': 38.91036605834961,
                'point_count': 2924,
                'longitude_origin': 0.0,
                'longitude_step': 0.12311901152133942
            },
            {
                'latitude': 38.84006881713867,
                'point_count': 2928,
                'longitude_origin': 0.0,
                'longitude_step': 0.12295082211494446
            },
            {
                'latitude': 38.769771575927734,
                'point_count': 2932,
                'longitude_origin': 0.0,
                'longitude_step': 0.12278307974338531
            },
            {
                'latitude': 38.69947052001953,
                'point_count': 2936,
                'longitude_origin': 0.0,
                'longitude_step': 0.12261580675840378
            },
            {
                'latitude': 38.629173278808594,
                'point_count': 2940,
                'longitude_origin': 0.0,
                'longitude_step': 0.12244898080825806
            },
            {
                'latitude': 38.55887222290039,
                'point_count': 2944,
                'longitude_origin': 0.0,
                'longitude_step': 0.12228260934352875
            },
            {
                'latitude': 38.48857498168945,
                'point_count': 2948,
                'longitude_origin': 0.0,
                'longitude_step': 0.12211669236421585
            },
            {
                'latitude': 38.418277740478516,
                'point_count': 2952,
                'longitude_origin': 0.0,
                'longitude_step': 0.12195122241973877
            },
            {
                'latitude': 38.34797668457031,
                'point_count': 2956,
                'longitude_origin': 0.0,
                'longitude_step': 0.1217861995100975
            },
            {
                'latitude': 38.277679443359375,
                'point_count': 2960,
                'longitude_origin': 0.0,
                'longitude_step': 0.12162162363529205
            },
            {
                'latitude': 38.20737838745117,
                'point_count': 2964,
                'longitude_origin': 0.0,
                'longitude_step': 0.12145748734474182
            },
            {
                'latitude': 38.137081146240234,
                'point_count': 2968,
                'longitude_origin': 0.0,
                'longitude_step': 0.1212937980890274
            },
            {
                'latitude': 38.0667839050293,
                'point_count': 2972,
                'longitude_origin': 0.0,
                'longitude_step': 0.1211305484175682
            },
            {
                'latitude': 37.996482849121094,
                'point_count': 2976,
                'longitude_origin': 0.0,
                'longitude_step': 0.12096773833036423
            },
            {
                'latitude': 37.926185607910156,
                'point_count': 2980,
                'longitude_origin': 0.0,
                'longitude_step': 0.12080536782741547
            },
            {
                'latitude': 37.85588455200195,
                'point_count': 2984,
                'longitude_origin': 0.0,
                'longitude_step': 0.12064342945814133
            },
            {
                'latitude': 37.785587310791016,
                'point_count': 2988,
                'longitude_origin': 0.0,
                'longitude_step': 0.1204819306731224
            },
            {
                'latitude': 37.71529006958008,
                'point_count': 2992,
                'longitude_origin': 0.0,
                'longitude_step': 0.12032085657119751
            },
            {
                'latitude': 37.644989013671875,
                'point_count': 2996,
                'longitude_origin': 0.0,
                'longitude_step': 0.12016021460294724
            },
            {
                'latitude': 37.57469177246094,
                'point_count': 3000,
                'longitude_origin': 0.0,
                'longitude_step': 0.11999999731779099
            },
            {
                'latitude': 37.504390716552734,
                'point_count': 3004,
                'longitude_origin': 0.0,
                'longitude_step': 0.11984021216630936
            },
            {
                'latitude': 37.4340934753418,
                'point_count': 3008,
                'longitude_origin': 0.0,
                'longitude_step': 0.11968085169792175
            },
            {
                'latitude': 37.36379623413086,
                'point_count': 3012,
                'longitude_origin': 0.0,
                'longitude_step': 0.11952191591262817
            },
            {
                'latitude': 37.293495178222656,
                'point_count': 3016,
                'longitude_origin': 0.0,
                'longitude_step': 0.11936339735984802
            },
            {
                'latitude': 37.22319793701172,
                'point_count': 3020,
                'longitude_origin': 0.0,
                'longitude_step': 0.1192052960395813
            },
            {
                'latitude': 37.152896881103516,
                'point_count': 3024,
                'longitude_origin': 0.0,
                'longitude_step': 0.1190476194024086
            },
            {
                'latitude': 37.08259963989258,
                'point_count': 3028,
                'longitude_origin': 0.0,
                'longitude_step': 0.11889035999774933
            },
            {
                'latitude': 37.01230239868164,
                'point_count': 3032,
                'longitude_origin': 0.0,
                'longitude_step': 0.11873351037502289
            },
            {
                'latitude': 36.94200134277344,
                'point_count': 3036,
                'longitude_origin': 0.0,
                'longitude_step': 0.11857707798480988
            },
            {
                'latitude': 36.8717041015625,
                'point_count': 3040,
                'longitude_origin': 0.0,
                'longitude_step': 0.1184210553765297
            },
            {
                'latitude': 36.8014030456543,
                'point_count': 3044,
                'longitude_origin': 0.0,
                'longitude_step': 0.11826544255018234
            },
            {
                'latitude': 36.73110580444336,
                'point_count': 3048,
                'longitude_origin': 0.0,
                'longitude_step': 0.11811023950576782
            },
            {
                'latitude': 36.66080856323242,
                'point_count': 3052,
                'longitude_origin': 0.0,
                'longitude_step': 0.11795543879270554
            },
            {
                'latitude': 36.59050750732422,
                'point_count': 3056,
                'longitude_origin': 0.0,
                'longitude_step': 0.11780104786157608
            },
            {
                'latitude': 36.52021026611328,
                'point_count': 3060,
                'longitude_origin': 0.0,
                'longitude_step': 0.11764705926179886
            },
            {
                'latitude': 36.44990921020508,
                'point_count': 3064,
                'longitude_origin': 0.0,
                'longitude_step': 0.11749347299337387
            },
            {
                'latitude': 36.37961196899414,
                'point_count': 3068,
                'longitude_origin': 0.0,
                'longitude_step': 0.11734028905630112
            },
            {
                'latitude': 36.3093147277832,
                'point_count': 3072,
                'longitude_origin': 0.0,
                'longitude_step': 0.1171875
            },
            {
                'latitude': 36.239013671875,
                'point_count': 3076,
                'longitude_origin': 0.0,
                'longitude_step': 0.11703511327505112
            },
            {
                'latitude': 36.16871643066406,
                'point_count': 3080,
                'longitude_origin': 0.0,
                'longitude_step': 0.11688311398029327
            },
            {
                'latitude': 36.09841537475586,
                'point_count': 3084,
                'longitude_origin': 0.0,
                'longitude_step': 0.11673151701688766
            },
            {
                'latitude': 36.02811813354492,
                'point_count': 3088,
                'longitude_origin': 0.0,
                'longitude_step': 0.1165803074836731
            },
            {
                'latitude': 35.957820892333984,
                'point_count': 3092,
                'longitude_origin': 0.0,
                'longitude_step': 0.11642949283123016
            },
            {
                'latitude': 35.88751983642578,
                'point_count': 3096,
                'longitude_origin': 0.0,
                'longitude_step': 0.11627907305955887
            },
            {
                'latitude': 35.817222595214844,
                'point_count': 3100,
                'longitude_origin': 0.0,
                'longitude_step': 0.11612903326749802
            },
            {
                'latitude': 35.74692153930664,
                'point_count': 3104,
                'longitude_origin': 0.0,
                'longitude_step': 0.1159793809056282
            },
            {
                'latitude': 35.6766242980957,
                'point_count': 3108,
                'longitude_origin': 0.0,
                'longitude_step': 0.11583011597394943
            },
            {
                'latitude': 35.606327056884766,
                'point_count': 3112,
                'longitude_origin': 0.0,
                'longitude_step': 0.1156812310218811
            },
            {
                'latitude': 35.53602600097656,
                'point_count': 3116,
                'longitude_origin': 0.0,
                'longitude_step': 0.11553273350000381
            },
            {
                'latitude': 35.465728759765625,
                'point_count': 3120,
                'longitude_origin': 0.0,
                'longitude_step': 0.11538461595773697
            },
            {
                'latitude': 35.39542770385742,
                'point_count': 3124,
                'longitude_origin': 0.0,
                'longitude_step': 0.11523687839508057
            },
            {
                'latitude': 35.325130462646484,
                'point_count': 3128,
                'longitude_origin': 0.0,
                'longitude_step': 0.11508951336145401
            },
            {
                'latitude': 35.25483322143555,
                'point_count': 3132,
                'longitude_origin': 0.0,
                'longitude_step': 0.1149425283074379
            },
            {
                'latitude': 35.184532165527344,
                'point_count': 3136,
                'longitude_origin': 0.0,
                'longitude_step': 0.11479591578245163
            },
            {
                'latitude': 35.114234924316406,
                'point_count': 3140,
                'longitude_origin': 0.0,
                'longitude_step': 0.1146496832370758
            },
            {
                'latitude': 35.0439338684082,
                'point_count': 3144,
                'longitude_origin': 0.0,
                'longitude_step': 0.11450381577014923
            },
            {
                'latitude': 34.973636627197266,
                'point_count': 3148,
                'longitude_origin': 0.0,
                'longitude_step': 0.1143583208322525
            },
            {
                'latitude': 34.90333938598633,
                'point_count': 3152,
                'longitude_origin': 0.0,
                'longitude_step': 0.11421319842338562
            },
            {
                'latitude': 34.833038330078125,
                'point_count': 3156,
                'longitude_origin': 0.0,
                'longitude_step': 0.11406844109296799
            },
            {
                'latitude': 34.76274108886719,
                'point_count': 3160,
                'longitude_origin': 0.0,
                'longitude_step': 0.1139240488409996
            },
            {
                'latitude': 34.692440032958984,
                'point_count': 3164,
                'longitude_origin': 0.0,
                'longitude_step': 0.11378002166748047
            },
            {
                'latitude': 34.62214279174805,
                'point_count': 3168,
                'longitude_origin': 0.0,
                'longitude_step': 0.11363636702299118
            },
            {
                'latitude': 34.55184555053711,
                'point_count': 3172,
                'longitude_origin': 0.0,
                'longitude_step': 0.11349306255578995
            },
            {
                'latitude': 34.481544494628906,
                'point_count': 3176,
                'longitude_origin': 0.0,
                'longitude_step': 0.11335012316703796
            },
            {
                'latitude': 34.41124725341797,
                'point_count': 3180,
                'longitude_origin': 0.0,
                'longitude_step': 0.11320754885673523
            },
            {
                'latitude': 34.340946197509766,
                'point_count': 3184,
                'longitude_origin': 0.0,
                'longitude_step': 0.11306532472372055
            },
            {
                'latitude': 34.27064895629883,
                'point_count': 3188,
                'longitude_origin': 0.0,
                'longitude_step': 0.11292346566915512
            },
            {
                'latitude': 34.20035171508789,
                'point_count': 3192,
                'longitude_origin': 0.0,
                'longitude_step': 0.11278195679187775
            },
            {
                'latitude': 34.13005065917969,
                'point_count': 3196,
                'longitude_origin': 0.0,
                'longitude_step': 0.11264079809188843
            },
            {
                'latitude': 34.05975341796875,
                'point_count': 3200,
                'longitude_origin': 0.0,
                'longitude_step': 0.11249999701976776
            },
            {
                'latitude': 33.98945236206055,
                'point_count': 3204,
                'longitude_origin': 0.0,
                'longitude_step': 0.11235955357551575
            },
            {
                'latitude': 33.91915512084961,
                'point_count': 3208,
                'longitude_origin': 0.0,
                'longitude_step': 0.11221945285797119
            },
            {
                'latitude': 33.84885787963867,
                'point_count': 3212,
                'longitude_origin': 0.0,
                'longitude_step': 0.11207970231771469
            },
            {
                'latitude': 33.77855682373047,
                'point_count': 3216,
                'longitude_origin': 0.0,
                'longitude_step': 0.11194030195474625
            },
            {
                'latitude': 33.70825958251953,
                'point_count': 3220,
                'longitude_origin': 0.0,
                'longitude_step': 0.11180124431848526
            },
            {
                'latitude': 33.63795852661133,
                'point_count': 3224,
                'longitude_origin': 0.0,
                'longitude_step': 0.11166252940893173
            },
            {
                'latitude': 33.56766128540039,
                'point_count': 3228,
                'longitude_origin': 0.0,
                'longitude_step': 0.11152416467666626
            },
            {
                'latitude': 33.49736404418945,
                'point_count': 3232,
                'longitude_origin': 0.0,
                'longitude_step': 0.11138613522052765
            },
            {
                'latitude': 33.42706298828125,
                'point_count': 3236,
                'longitude_origin': 0.0,
                'longitude_step': 0.1112484559416771
            },
            {
                'latitude': 33.35676574707031,
                'point_count': 3240,
                'longitude_origin': 0.0,
                'longitude_step': 0.1111111119389534
            },
            {
                'latitude': 33.28646469116211,
                'point_count': 3244,
                'longitude_origin': 0.0,
                'longitude_step': 0.11097410321235657
            },
            {
                'latitude': 33.21616744995117,
                'point_count': 3248,
                'longitude_origin': 0.0,
                'longitude_step': 0.1108374372124672
            },
            {
                'latitude': 33.145870208740234,
                'point_count': 3252,
                'longitude_origin': 0.0,
                'longitude_step': 0.11070110648870468
            },
            {
                'latitude': 33.07556915283203,
                'point_count': 3256,
                'longitude_origin': 0.0,
                'longitude_step': 0.11056511104106903
            },
            {
                'latitude': 33.005271911621094,
                'point_count': 3260,
                'longitude_origin': 0.0,
                'longitude_step': 0.11042945086956024
            },
            {
                'latitude': 32.93497085571289,
                'point_count': 3264,
                'longitude_origin': 0.0,
                'longitude_step': 0.11029411852359772
            },
            {
                'latitude': 32.86467361450195,
                'point_count': 3268,
                'longitude_origin': 0.0,
                'longitude_step': 0.11015912145376205
            },
            {
                'latitude': 32.794376373291016,
                'point_count': 3272,
                'longitude_origin': 0.0,
                'longitude_step': 0.11002445220947266
            },
            {
                'latitude': 32.72407531738281,
                'point_count': 3276,
                'longitude_origin': 0.0,
                'longitude_step': 0.10989011079072952
            },
            {
                'latitude': 32.653778076171875,
                'point_count': 3280,
                'longitude_origin': 0.0,
                'longitude_step': 0.10975609719753265
            },
            {
                'latitude': 32.58347702026367,
                'point_count': 3284,
                'longitude_origin': 0.0,
                'longitude_step': 0.10962241142988205
            },
            {
                'latitude': 32.513179779052734,
                'point_count': 3288,
                'longitude_origin': 0.0,
                'longitude_step': 0.10948905348777771
            },
            {
                'latitude': 32.4428825378418,
                'point_count': 3292,
                'longitude_origin': 0.0,
                'longitude_step': 0.10935601592063904
            },
            {
                'latitude': 32.372581481933594,
                'point_count': 3296,
                'longitude_origin': 0.0,
                'longitude_step': 0.10922329872846603
            },
            {
                'latitude': 32.302284240722656,
                'point_count': 3300,
                'longitude_origin': 0.0,
                'longitude_step': 0.1090909093618393
            },
            {
                'latitude': 32.23198318481445,
                'point_count': 3304,
                'longitude_origin': 0.0,
                'longitude_step': 0.10895884037017822
            },
            {
                'latitude': 32.161685943603516,
                'point_count': 3308,
                'longitude_origin': 0.0,
                'longitude_step': 0.10882708430290222
            },
            {
                'latitude': 32.09138870239258,
                'point_count': 3312,
                'longitude_origin': 0.0,
                'longitude_step': 0.10869564861059189
            },
            {
                'latitude': 32.021087646484375,
                'point_count': 3316,
                'longitude_origin': 0.0,
                'longitude_step': 0.10856453329324722
            },
            {
                'latitude': 31.950790405273438,
                'point_count': 3320,
                'longitude_origin': 0.0,
                'longitude_step': 0.10843373835086823
            },
            {
                'latitude': 31.880491256713867,
                'point_count': 3324,
                'longitude_origin': 0.0,
                'longitude_step': 0.1083032488822937
            },
            {
                'latitude': 31.810192108154297,
                'point_count': 3328,
                'longitude_origin': 0.0,
                'longitude_step': 0.10817307978868484
            },
            {
                'latitude': 31.739892959594727,
                'point_count': 3332,
                'longitude_origin': 0.0,
                'longitude_step': 0.10804321616888046
            },
            {
                'latitude': 31.669593811035156,
                'point_count': 3336,
                'longitude_origin': 0.0,
                'longitude_step': 0.10791366547346115
            },
            {
                'latitude': 31.59929656982422,
                'point_count': 3340,
                'longitude_origin': 0.0,
                'longitude_step': 0.10778442770242691
            },
            {
                'latitude': 31.52899742126465,
                'point_count': 3344,
                'longitude_origin': 0.0,
                'longitude_step': 0.10765550285577774
            },
            {
                'latitude': 31.458698272705078,
                'point_count': 3348,
                'longitude_origin': 0.0,
                'longitude_step': 0.10752688348293304
            },
            {
                'latitude': 31.388399124145508,
                'point_count': 3352,
                'longitude_origin': 0.0,
                'longitude_step': 0.10739856958389282
            },
            {
                'latitude': 31.318099975585938,
                'point_count': 3356,
                'longitude_origin': 0.0,
                'longitude_step': 0.10727056115865707
            },
            {
                'latitude': 31.247802734375,
                'point_count': 3360,
                'longitude_origin': 0.0,
                'longitude_step': 0.1071428582072258
            },
            {
                'latitude': 31.17750358581543,
                'point_count': 3364,
                'longitude_origin': 0.0,
                'longitude_step': 0.107015460729599
            },
            {
                'latitude': 31.10720443725586,
                'point_count': 3368,
                'longitude_origin': 0.0,
                'longitude_step': 0.10688836127519608
            },
            {
                'latitude': 31.03690528869629,
                'point_count': 3372,
                'longitude_origin': 0.0,
                'longitude_step': 0.10676156729459763
            },
            {
                'latitude': 30.96660614013672,
                'point_count': 3376,
                'longitude_origin': 0.0,
                'longitude_step': 0.10663507133722305
            },
            {
                'latitude': 30.89630889892578,
                'point_count': 3380,
                'longitude_origin': 0.0,
                'longitude_step': 0.10650887340307236
            },
            {
                'latitude': 30.82600975036621,
                'point_count': 3384,
                'longitude_origin': 0.0,
                'longitude_step': 0.10638298094272614
            },
            {
                'latitude': 30.75571060180664,
                'point_count': 3388,
                'longitude_origin': 0.0,
                'longitude_step': 0.1062573790550232
            },
            {
                'latitude': 30.68541145324707,
                'point_count': 3392,
                'longitude_origin': 0.0,
                'longitude_step': 0.10613207519054413
            },
            {
                'latitude': 30.6151123046875,
                'point_count': 3396,
                'longitude_origin': 0.0,
                'longitude_step': 0.10600706934928894
            },
            {
                'latitude': 30.544815063476562,
                'point_count': 3400,
                'longitude_origin': 0.0,
                'longitude_step': 0.10588235408067703
            },
            {
                'latitude': 30.474515914916992,
                'point_count': 3404,
                'longitude_origin': 0.0,
                'longitude_step': 0.1057579293847084
            },
            {
                'latitude': 30.404216766357422,
                'point_count': 3408,
                'longitude_origin': 0.0,
                'longitude_step': 0.10563380271196365
            },
            {
                'latitude': 30.33391761779785,
                'point_count': 3412,
                'longitude_origin': 0.0,
                'longitude_step': 0.10550996661186218
            },
            {
                'latitude': 30.26361846923828,
                'point_count': 3416,
                'longitude_origin': 0.0,
                'longitude_step': 0.1053864136338234
            },
            {
                'latitude': 30.193321228027344,
                'point_count': 3420,
                'longitude_origin': 0.0,
                'longitude_step': 0.10526315867900848
            },
            {
                'latitude': 30.123022079467773,
                'point_count': 3424,
                'longitude_origin': 0.0,
                'longitude_step': 0.10514018684625626
            },
            {
                'latitude': 30.052722930908203,
                'point_count': 3428,
                'longitude_origin': 0.0,
                'longitude_step': 0.10501750558614731
            },
            {
                'latitude': 29.982423782348633,
                'point_count': 3432,
                'longitude_origin': 0.0,
                'longitude_step': 0.10489510744810104
            },
            {
                'latitude': 29.912124633789062,
                'point_count': 3436,
                'longitude_origin': 0.0,
                'longitude_step': 0.10477299243211746
            },
            {
                'latitude': 29.841827392578125,
                'point_count': 3440,
                'longitude_origin': 0.0,
                'longitude_step': 0.10465116053819656
            },
            {
                'latitude': 29.771528244018555,
                'point_count': 3444,
                'longitude_origin': 0.0,
                'longitude_step': 0.10452961921691895
            },
            {
                'latitude': 29.701229095458984,
                'point_count': 3448,
                'longitude_origin': 0.0,
                'longitude_step': 0.10440835356712341
            },
            {
                'latitude': 29.630929946899414,
                'point_count': 3452,
                'longitude_origin': 0.0,
                'longitude_step': 0.10428737103939056
            },
            {
                'latitude': 29.560630798339844,
                'point_count': 3456,
                'longitude_origin': 0.0,
                'longitude_step': 0.1041666641831398
            },
            {
                'latitude': 29.490333557128906,
                'point_count': 3460,
                'longitude_origin': 0.0,
                'longitude_step': 0.10404624044895172
            },
            {
                'latitude': 29.420034408569336,
                'point_count': 3464,
                'longitude_origin': 0.0,
                'longitude_step': 0.10392609983682632
            },
            {
                'latitude': 29.349735260009766,
                'point_count': 3468,
                'longitude_origin': 0.0,
                'longitude_step': 0.10380622744560242
            },
            {
                'latitude': 29.279436111450195,
                'point_count': 3472,
                'longitude_origin': 0.0,
                'longitude_step': 0.10368663817644119
            },
            {
                'latitude': 29.209136962890625,
                'point_count': 3476,
                'longitude_origin': 0.0,
                'longitude_step': 0.10356731712818146
            },
            {
                'latitude': 29.138839721679688,
                'point_count': 3480,
                'longitude_origin': 0.0,
                'longitude_step': 0.1034482792019844
            },
            {
                'latitude': 29.068540573120117,
                'point_count': 3484,
                'longitude_origin': 0.0,
                'longitude_step': 0.10332950949668884
            },
            {
                'latitude': 28.998241424560547,
                'point_count': 3488,
                'longitude_origin': 0.0,
                'longitude_step': 0.10321100801229477
            },
            {
                'latitude': 28.927942276000977,
                'point_count': 3492,
                'longitude_origin': 0.0,
                'longitude_step': 0.10309278219938278
            },
            {
                'latitude': 28.857643127441406,
                'point_count': 3496,
                'longitude_origin': 0.0,
                'longitude_step': 0.10297483205795288
            },
            {
                'latitude': 28.78734588623047,
                'point_count': 3500,
                'longitude_origin': 0.0,
                'longitude_step': 0.10285714268684387
            },
            {
                'latitude': 28.7170467376709,
                'point_count': 3504,
                'longitude_origin': 0.0,
                'longitude_step': 0.10273972898721695
            },
            {
                'latitude': 28.646747589111328,
                'point_count': 3508,
                'longitude_origin': 0.0,
                'longitude_step': 0.10262257605791092
            },
            {
                'latitude': 28.576448440551758,
                'point_count': 3512,
                'longitude_origin': 0.0,
                'longitude_step': 0.10250569134950638
            },
            {
                'latitude': 28.506149291992188,
                'point_count': 3516,
                'longitude_origin': 0.0,
                'longitude_step': 0.10238907486200333
            },
            {
                'latitude': 28.43585205078125,
                'point_count': 3520,
                'longitude_origin': 0.0,
                'longitude_step': 0.10227272659540176
            },
            {
                'latitude': 28.36555290222168,
                'point_count': 3524,
                'longitude_origin': 0.0,
                'longitude_step': 0.1021566390991211
            },
            {
                'latitude': 28.29525375366211,
                'point_count': 3528,
                'longitude_origin': 0.0,
                'longitude_step': 0.10204081982374191
            },
            {
                'latitude': 28.22495460510254,
                'point_count': 3532,
                'longitude_origin': 0.0,
                'longitude_step': 0.10192525386810303
            },
            {
                'latitude': 28.15465545654297,
                'point_count': 3536,
                'longitude_origin': 0.0,
                'longitude_step': 0.10180995613336563
            },
            {
                'latitude': 28.08435821533203,
                'point_count': 3540,
                'longitude_origin': 0.0,
                'longitude_step': 0.10169491171836853
            },
            {
                'latitude': 28.01405906677246,
                'point_count': 3544,
                'longitude_origin': 0.0,
                'longitude_step': 0.10158013552427292
            },
            {
                'latitude': 27.94375991821289,
                'point_count': 3548,
                'longitude_origin': 0.0,
                'longitude_step': 0.1014656126499176
            },
            {
                'latitude': 27.87346076965332,
                'point_count': 3552,
                'longitude_origin': 0.0,
                'longitude_step': 0.10135135054588318
            },
            {
                'latitude': 27.80316162109375,
                'point_count': 3556,
                'longitude_origin': 0.0,
                'longitude_step': 0.10123734176158905
            },
            {
                'latitude': 27.732864379882812,
                'point_count': 3560,
                'longitude_origin': 0.0,
                'longitude_step': 0.10112359374761581
            },
            {
                'latitude': 27.662565231323242,
                'point_count': 3564,
                'longitude_origin': 0.0,
                'longitude_step': 0.10101009905338287
            },
            {
                'latitude': 27.592266082763672,
                'point_count': 3568,
                'longitude_origin': 0.0,
                'longitude_step': 0.10089685767889023
            },
            {
                'latitude': 27.5219669342041,
                'point_count': 3572,
                'longitude_origin': 0.0,
                'longitude_step': 0.10078387707471848
            },
            {
                'latitude': 27.45166778564453,
                'point_count': 3576,
                'longitude_origin': 0.0,
                'longitude_step': 0.10067114233970642
            },
            {
                'latitude': 27.381370544433594,
                'point_count': 3580,
                'longitude_origin': 0.0,
                'longitude_step': 0.10055866092443466
            },
            {
                'latitude': 27.311071395874023,
                'point_count': 3584,
                'longitude_origin': 0.0,
                'longitude_step': 0.1004464253783226
            },
            {
                'latitude': 27.240772247314453,
                'point_count': 3588,
                'longitude_origin': 0.0,
                'longitude_step': 0.10033445060253143
            },
            {
                'latitude': 27.170473098754883,
                'point_count': 3592,
                'longitude_origin': 0.0,
                'longitude_step': 0.10022271424531937
            },
            {
                'latitude': 27.100173950195312,
                'point_count': 3596,
                'longitude_origin': 0.0,
                'longitude_step': 0.1001112312078476
            },
            {
                'latitude': 27.029876708984375,
                'point_count': 3600,
                'longitude_origin': 0.0,
                'longitude_step': 0.10000000149011612
            },
            {
                'latitude': 26.959577560424805,
                'point_count': 3604,
                'longitude_origin': 0.0,
                'longitude_step': 0.09988901019096375
            },
            {
                'latitude': 26.889278411865234,
                'point_count': 3608,
                'longitude_origin': 0.0,
                'longitude_step': 0.09977827221155167
            },
            {
                'latitude': 26.818979263305664,
                'point_count': 3612,
                'longitude_origin': 0.0,
                'longitude_step': 0.09966777265071869
            },
            {
                'latitude': 26.748680114746094,
                'point_count': 3616,
                'longitude_origin': 0.0,
                'longitude_step': 0.09955751895904541
            },
            {
                'latitude': 26.678380966186523,
                'point_count': 3620,
                'longitude_origin': 0.0,
                'longitude_step': 0.09944751113653183
            },
            {
                'latitude': 26.608083724975586,
                'point_count': 3624,
                'longitude_origin': 0.0,
                'longitude_step': 0.09933774918317795
            },
            {
                'latitude': 26.537784576416016,
                'point_count': 3628,
                'longitude_origin': 0.0,
                'longitude_step': 0.09922822564840317
            },
            {
                'latitude': 26.467485427856445,
                'point_count': 3632,
                'longitude_origin': 0.0,
                'longitude_step': 0.09911894053220749
            },
            {
                'latitude': 26.397186279296875,
                'point_count': 3636,
                'longitude_origin': 0.0,
                'longitude_step': 0.09900990128517151
            },
            {
                'latitude': 26.326887130737305,
                'point_count': 3640,
                'longitude_origin': 0.0,
                'longitude_step': 0.09890110045671463
            },
            {
                'latitude': 26.256589889526367,
                'point_count': 3644,
                'longitude_origin': 0.0,
                'longitude_step': 0.09879253804683685
            },
            {
                'latitude': 26.186290740966797,
                'point_count': 3648,
                'longitude_origin': 0.0,
                'longitude_step': 0.09868421405553818
            },
            {
                'latitude': 26.115991592407227,
                'point_count': 3652,
                'longitude_origin': 0.0,
                'longitude_step': 0.098576121032238
            },
            {
                'latitude': 26.045692443847656,
                'point_count': 3656,
                'longitude_origin': 0.0,
                'longitude_step': 0.09846827387809753
            },
            {
                'latitude': 25.975393295288086,
                'point_count': 3660,
                'longitude_origin': 0.0,
                'longitude_step': 0.09836065769195557
            },
            {
                'latitude': 25.90509605407715,
                'point_count': 3664,
                'longitude_origin': 0.0,
                'longitude_step': 0.0982532724738121
            },
            {
                'latitude': 25.834796905517578,
                'point_count': 3668,
                'longitude_origin': 0.0,
                'longitude_step': 0.09814612567424774
            },
            {
                'latitude': 25.764497756958008,
                'point_count': 3672,
                'longitude_origin': 0.0,
                'longitude_step': 0.09803921729326248
            },
            {
                'latitude': 25.694198608398438,
                'point_count': 3676,
                'longitude_origin': 0.0,
                'longitude_step': 0.09793253242969513
            },
            {
                'latitude': 25.623899459838867,
                'point_count': 3680,
                'longitude_origin': 0.0,
                'longitude_step': 0.09782608598470688
            },
            {
                'latitude': 25.55360221862793,
                'point_count': 3684,
                'longitude_origin': 0.0,
                'longitude_step': 0.09771987050771713
            },
            {
                'latitude': 25.48330307006836,
                'point_count': 3688,
                'longitude_origin': 0.0,
                'longitude_step': 0.09761388599872589
            },
            {
                'latitude': 25.41300392150879,
                'point_count': 3692,
                'longitude_origin': 0.0,
                'longitude_step': 0.09750812500715256
            },
            {
                'latitude': 25.34270477294922,
                'point_count': 3696,
                'longitude_origin': 0.0,
                'longitude_step': 0.09740259498357773
            },
            {
                'latitude': 25.27240562438965,
                'point_count': 3700,
                'longitude_origin': 0.0,
                'longitude_step': 0.0972972959280014
            },
            {
                'latitude': 25.20210838317871,
                'point_count': 3704,
                'longitude_origin': 0.0,
                'longitude_step': 0.09719222784042358
            },
            {
                'latitude': 25.13180923461914,
                'point_count': 3708,
                'longitude_origin': 0.0,
                'longitude_step': 0.09708737581968307
            },
            {
                'latitude': 25.06151008605957,
                'point_count': 3712,
                'longitude_origin': 0.0,
                'longitude_step': 0.09698276221752167
            },
            {
                'latitude': 24.9912109375,
                'point_count': 3716,
                'longitude_origin': 0.0,
                'longitude_step': 0.09687836468219757
            },
            {
                'latitude': 24.92091178894043,
                'point_count': 3720,
                'longitude_origin': 0.0,
                'longitude_step': 0.09677419066429138
            },
            {
                'latitude': 24.850614547729492,
                'point_count': 3724,
                'longitude_origin': 0.0,
                'longitude_step': 0.0966702476143837
            },
            {
                'latitude': 24.780315399169922,
                'point_count': 3728,
                'longitude_origin': 0.0,
                'longitude_step': 0.09656652063131332
            },
            {
                'latitude': 24.71001625061035,
                'point_count': 3732,
                'longitude_origin': 0.0,
                'longitude_step': 0.09646302461624146
            },
            {
                'latitude': 24.63971710205078,
                'point_count': 3736,
                'longitude_origin': 0.0,
                'longitude_step': 0.0963597446680069
            },
            {
                'latitude': 24.56941795349121,
                'point_count': 3740,
                'longitude_origin': 0.0,
                'longitude_step': 0.09625668078660965
            },
            {
                'latitude': 24.499120712280273,
                'point_count': 3744,
                'longitude_origin': 0.0,
                'longitude_step': 0.09615384787321091
            },
            {
                'latitude': 24.428821563720703,
                'point_count': 3748,
                'longitude_origin': 0.0,
                'longitude_step': 0.09605123102664948
            },
            {
                'latitude': 24.358522415161133,
                'point_count': 3752,
                'longitude_origin': 0.0,
                'longitude_step': 0.09594883024692535
            },
            {
                'latitude': 24.288223266601562,
                'point_count': 3756,
                'longitude_origin': 0.0,
                'longitude_step': 0.09584664553403854
            },
            {
                'latitude': 24.217924118041992,
                'point_count': 3760,
                'longitude_origin': 0.0,
                'longitude_step': 0.09574468433856964
            },
            {
                'latitude': 24.147626876831055,
                'point_count': 3764,
                'longitude_origin': 0.0,
                'longitude_step': 0.09564293175935745
            },
            {
                'latitude': 24.077327728271484,
                'point_count': 3768,
                'longitude_origin': 0.0,
                'longitude_step': 0.09554140269756317
            },
            {
                'latitude': 24.007028579711914,
                'point_count': 3772,
                'longitude_origin': 0.0,
                'longitude_step': 0.0954400822520256
            },
            {
                'latitude': 23.936729431152344,
                'point_count': 3776,
                'longitude_origin': 0.0,
                'longitude_step': 0.09533898532390594
            },
            {
                'latitude': 23.866430282592773,
                'point_count': 3780,
                'longitude_origin': 0.0,
                'longitude_step': 0.095238097012043
            },
            {
                'latitude': 23.796133041381836,
                'point_count': 3784,
                'longitude_origin': 0.0,
                'longitude_step': 0.09513741731643677
            },
            {
                'latitude': 23.725833892822266,
                'point_count': 3788,
                'longitude_origin': 0.0,
                'longitude_step': 0.09503696113824844
            },
            {
                'latitude': 23.655534744262695,
                'point_count': 3792,
                'longitude_origin': 0.0,
                'longitude_step': 0.09493670612573624
            },
            {
                'latitude': 23.585235595703125,
                'point_count': 3796,
                'longitude_origin': 0.0,
                'longitude_step': 0.09483666718006134
            },
            {
                'latitude': 23.514936447143555,
                'point_count': 3800,
                'longitude_origin': 0.0,
                'longitude_step': 0.09473684430122375
            },
            {
                'latitude': 23.444639205932617,
                'point_count': 3804,
                'longitude_origin': 0.0,
                'longitude_step': 0.09463722258806229
            },
            {
                'latitude': 23.374340057373047,
                'point_count': 3808,
                'longitude_origin': 0.0,
                'longitude_step': 0.09453781694173813
            },
            {
                'latitude': 23.304040908813477,
                'point_count': 3812,
                'longitude_origin': 0.0,
                'longitude_step': 0.09443861246109009
            },
            {
                'latitude': 23.233741760253906,
                'point_count': 3816,
                'longitude_origin': 0.0,
                'longitude_step': 0.09433962404727936
            },
            {
                'latitude': 23.163442611694336,
                'point_count': 3820,
                'longitude_origin': 0.0,
                'longitude_step': 0.09424083679914474
            },
            {
                'latitude': 23.0931453704834,
                'point_count': 3824,
                'longitude_origin': 0.0,
                'longitude_step': 0.09414225816726685
            },
            {
                'latitude': 23.022846221923828,
                'point_count': 3828,
                'longitude_origin': 0.0,
                'longitude_step': 0.09404388815164566
            },
            {
                'latitude': 22.952547073364258,
                'point_count': 3832,
                'longitude_origin': 0.0,
                'longitude_step': 0.09394571930170059
            },
            {
                'latitude': 22.882247924804688,
                'point_count': 3836,
                'longitude_origin': 0.0,
                'longitude_step': 0.09384775906801224
            },
            {
                'latitude': 22.811948776245117,
                'point_count': 3840,
                'longitude_origin': 0.0,
                'longitude_step': 0.09375
            },
            {
                'latitude': 22.74165153503418,
                'point_count': 3844,
                'longitude_origin': 0.0,
                'longitude_step': 0.09365244209766388
            },
            {
                'latitude': 22.67135238647461,
                'point_count': 3848,
                'longitude_origin': 0.0,
                'longitude_step': 0.09355509281158447
            },
            {
                'latitude': 22.60105323791504,
                'point_count': 3852,
                'longitude_origin': 0.0,
                'longitude_step': 0.09345794469118118
            },
            {
                'latitude': 22.53075408935547,
                'point_count': 3856,
                'longitude_origin': 0.0,
                'longitude_step': 0.09336099773645401
            },
            {
                'latitude': 22.4604549407959,
                'point_count': 3860,
                'longitude_origin': 0.0,
                'longitude_step': 0.09326425194740295
            },
            {
                'latitude': 22.39015769958496,
                'point_count': 3864,
                'longitude_origin': 0.0,
                'longitude_step': 0.09316769987344742
            },
            {
                'latitude': 22.31985855102539,
                'point_count': 3868,
                'longitude_origin': 0.0,
                'longitude_step': 0.0930713564157486
            },
            {
                'latitude': 22.24955940246582,
                'point_count': 3872,
                'longitude_origin': 0.0,
                'longitude_step': 0.0929752066731453
            },
            {
                'latitude': 22.17926025390625,
                'point_count': 3876,
                'longitude_origin': 0.0,
                'longitude_step': 0.09287925809621811
            },
            {
                'latitude': 22.10896110534668,
                'point_count': 3880,
                'longitude_origin': 0.0,
                'longitude_step': 0.09278350323438644
            },
            {
                'latitude': 22.038663864135742,
                'point_count': 3884,
                'longitude_origin': 0.0,
                'longitude_step': 0.0926879495382309
            },
            {
                'latitude': 21.968364715576172,
                'point_count': 3888,
                'longitude_origin': 0.0,
                'longitude_step': 0.09259258955717087
            },
            {
                'latitude': 21.8980655670166,
                'point_count': 3892,
                'longitude_origin': 0.0,
                'longitude_step': 0.09249743074178696
            },
            {
                'latitude': 21.82776641845703,
                'point_count': 3896,
                'longitude_origin': 0.0,
                'longitude_step': 0.09240246564149857
            },
            {
                'latitude': 21.75746726989746,
                'point_count': 3900,
                'longitude_origin': 0.0,
                'longitude_step': 0.0923076942563057
            },
            {
                'latitude': 21.687170028686523,
                'point_count': 3904,
                'longitude_origin': 0.0,
                'longitude_step': 0.09221311658620834
            },
            {
                'latitude': 21.616870880126953,
                'point_count': 3908,
                'longitude_origin': 0.0,
                'longitude_step': 0.09211873263120651
            },
            {
                'latitude': 21.546571731567383,
                'point_count': 3912,
                'longitude_origin': 0.0,
                'longitude_step': 0.0920245423913002
            },
            {
                'latitude': 21.476272583007812,
                'point_count': 3916,
                'longitude_origin': 0.0,
                'longitude_step': 0.09193053841590881
            },
            {
                'latitude': 21.405973434448242,
                'point_count': 3920,
                'longitude_origin': 0.0,
                'longitude_step': 0.09183673560619354
            },
            {
                'latitude': 21.335676193237305,
                'point_count': 3924,
                'longitude_origin': 0.0,
                'longitude_step': 0.0917431190609932
            },
            {
                'latitude': 21.265377044677734,
                'point_count': 3928,
                'longitude_origin': 0.0,
                'longitude_step': 0.09164969623088837
            },
            {
                'latitude': 21.195077896118164,
                'point_count': 3932,
                'longitude_origin': 0.0,
                'longitude_step': 0.09155645966529846
            },
            {
                'latitude': 21.124778747558594,
                'point_count': 3936,
                'longitude_origin': 0.0,
                'longitude_step': 0.09146341681480408
            },
            {
                'latitude': 21.054479598999023,
                'point_count': 3940,
                'longitude_origin': 0.0,
                'longitude_step': 0.09137056022882462
            },
            {
                'latitude': 20.984182357788086,
                'point_count': 3944,
                'longitude_origin': 0.0,
                'longitude_step': 0.09127788990736008
            },
            {
                'latitude': 20.913883209228516,
                'point_count': 3948,
                'longitude_origin': 0.0,
                'longitude_step': 0.09118541330099106
            },
            {
                'latitude': 20.843584060668945,
                'point_count': 3952,
                'longitude_origin': 0.0,
                'longitude_step': 0.09109311550855637
            },
            {
                'latitude': 20.773284912109375,
                'point_count': 3956,
                'longitude_origin': 0.0,
                'longitude_step': 0.0910010114312172
            },
            {
                'latitude': 20.702985763549805,
                'point_count': 3960,
                'longitude_origin': 0.0,
                'longitude_step': 0.09090909361839294
            },
            {
                'latitude': 20.632688522338867,
                'point_count': 3964,
                'longitude_origin': 0.0,
                'longitude_step': 0.09081735461950302
            },
            {
                'latitude': 20.562389373779297,
                'point_count': 3968,
                'longitude_origin': 0.0,
                'longitude_step': 0.09072580933570862
            },
            {
                'latitude': 20.492090225219727,
                'point_count': 3972,
                'longitude_origin': 0.0,
                'longitude_step': 0.09063444286584854
            },
            {
                'latitude': 20.421791076660156,
                'point_count': 3976,
                'longitude_origin': 0.0,
                'longitude_step': 0.09054326266050339
            },
            {
                'latitude': 20.351491928100586,
                'point_count': 3980,
                'longitude_origin': 0.0,
                'longitude_step': 0.09045226126909256
            },
            {
                'latitude': 20.28119468688965,
                'point_count': 3984,
                'longitude_origin': 0.0,
                'longitude_step': 0.09036144614219666
            },
            {
                'latitude': 20.210895538330078,
                'point_count': 3988,
                'longitude_origin': 0.0,
                'longitude_step': 0.09027080982923508
            },
            {
                'latitude': 20.140596389770508,
                'point_count': 3992,
                'longitude_origin': 0.0,
                'longitude_step': 0.09018035978078842
            },
            {
                'latitude': 20.070297241210938,
                'point_count': 3996,
                'longitude_origin': 0.0,
                'longitude_step': 0.09009008854627609
            },
            {
                'latitude': 19.999998092651367,
                'point_count': 4000,
                'longitude_origin': 0.0,
                'longitude_step': 0.09000000357627869
            },
            {
                'latitude': 19.92970085144043,
                'point_count': 4004,
                'longitude_origin': 0.0,
                'longitude_step': 0.08991008996963501
            },
            {
                'latitude': 19.85940170288086,
                'point_count': 4008,
                'longitude_origin': 0.0,
                'longitude_step': 0.08982036262750626
            },
            {
                'latitude': 19.78910255432129,
                'point_count': 4012,
                'longitude_origin': 0.0,
                'longitude_step': 0.08973080664873123
            },
            {
                'latitude': 19.71880340576172,
                'point_count': 4016,
                'longitude_origin': 0.0,
                'longitude_step': 0.08964143693447113
            },
            {
                'latitude': 19.64850425720215,
                'point_count': 4020,
                'longitude_origin': 0.0,
                'longitude_step': 0.08955223858356476
            },
            {
                'latitude': 19.57820701599121,
                'point_count': 4024,
                'longitude_origin': 0.0,
                'longitude_step': 0.08946321904659271
            },
            {
                'latitude': 19.50790786743164,
                'point_count': 4028,
                'longitude_origin': 0.0,
                'longitude_step': 0.08937437832355499
            },
            {
                'latitude': 19.43760871887207,
                'point_count': 4032,
                'longitude_origin': 0.0,
                'longitude_step': 0.0892857164144516
            },
            {
                'latitude': 19.3673095703125,
                'point_count': 4036,
                'longitude_origin': 0.0,
                'longitude_step': 0.08919722586870193
            },
            {
                'latitude': 19.29701042175293,
                'point_count': 4040,
                'longitude_origin': 0.0,
                'longitude_step': 0.0891089141368866
            },
            {
                'latitude': 19.226713180541992,
                'point_count': 4044,
                'longitude_origin': 0.0,
                'longitude_step': 0.08902077376842499
            },
            {
                'latitude': 19.156414031982422,
                'point_count': 4048,
                'longitude_origin': 0.0,
                'longitude_step': 0.08893280476331711
            },
            {
                'latitude': 19.08611488342285,
                'point_count': 4052,
                'longitude_origin': 0.0,
                'longitude_step': 0.08884501457214355
            },
            {
                'latitude': 19.01581573486328,
                'point_count': 4056,
                'longitude_origin': 0.0,
                'longitude_step': 0.08875739574432373
            },
            {
                'latitude': 18.94551658630371,
                'point_count': 4060,
                'longitude_origin': 0.0,
                'longitude_step': 0.08866994827985764
            },
            {
                'latitude': 18.875219345092773,
                'point_count': 4064,
                'longitude_origin': 0.0,
                'longitude_step': 0.08858267962932587
            },
            {
                'latitude': 18.804920196533203,
                'point_count': 4068,
                'longitude_origin': 0.0,
                'longitude_step': 0.08849557489156723
            },
            {
                'latitude': 18.734621047973633,
                'point_count': 4072,
                'longitude_origin': 0.0,
                'longitude_step': 0.08840864151716232
            },
            {
                'latitude': 18.664321899414062,
                'point_count': 4076,
                'longitude_origin': 0.0,
                'longitude_step': 0.08832188695669174
            },
            {
                'latitude': 18.594022750854492,
                'point_count': 4080,
                'longitude_origin': 0.0,
                'longitude_step': 0.0882352963089943
            },
            {
                'latitude': 18.523725509643555,
                'point_count': 4084,
                'longitude_origin': 0.0,
                'longitude_step': 0.08814887702465057
            },
            {
                'latitude': 18.453426361083984,
                'point_count': 4088,
                'longitude_origin': 0.0,
                'longitude_step': 0.08806262165307999
            },
            {
                'latitude': 18.383127212524414,
                'point_count': 4092,
                'longitude_origin': 0.0,
                'longitude_step': 0.08797653764486313
            },
            {
                'latitude': 18.312828063964844,
                'point_count': 4096,
                'longitude_origin': 0.0,
                'longitude_step': 0.087890625
            },
            {
                'latitude': 18.242528915405273,
                'point_count': 4100,
                'longitude_origin': 0.0,
                'longitude_step': 0.08780487626791
            },
            {
                'latitude': 18.172231674194336,
                'point_count': 4104,
                'longitude_origin': 0.0,
                'longitude_step': 0.08771929889917374
            },
            {
                'latitude': 18.101932525634766,
                'point_count': 4108,
                'longitude_origin': 0.0,
                'longitude_step': 0.0876338854432106
            },
            {
                'latitude': 18.031633377075195,
                'point_count': 4112,
                'longitude_origin': 0.0,
                'longitude_step': 0.0875486359000206
            },
            {
                'latitude': 17.961334228515625,
                'point_count': 4116,
                'longitude_origin': 0.0,
                'longitude_step': 0.08746355772018433
            },
            {
                'latitude': 17.891035079956055,
                'point_count': 4120,
                'longitude_origin': 0.0,
                'longitude_step': 0.08737864345312119
            },
            {
                'latitude': 17.820737838745117,
                'point_count': 4124,
                'longitude_origin': 0.0,
                'longitude_step': 0.08729389309883118
            },
            {
                'latitude': 17.750438690185547,
                'point_count': 4128,
                'longitude_origin': 0.0,
                'longitude_step': 0.0872092992067337
            },
            {
                'latitude': 17.680139541625977,
                'point_count': 4132,
                'longitude_origin': 0.0,
                'longitude_step': 0.08712487667798996
            },
            {
                'latitude': 17.609840393066406,
                'point_count': 4136,
                'longitude_origin': 0.0,
                'longitude_step': 0.08704061806201935
            },
            {
                'latitude': 17.539541244506836,
                'point_count': 4140,
                'longitude_origin': 0.0,
                'longitude_step': 0.08695652335882187
            },
            {
                'latitude': 17.4692440032959,
                'point_count': 4144,
                'longitude_origin': 0.0,
                'longitude_step': 0.08687258511781693
            },
            {
                'latitude': 17.398944854736328,
                'point_count': 4148,
                'longitude_origin': 0.0,
                'longitude_step': 0.08678881078958511
            },
            {
                'latitude': 17.328645706176758,
                'point_count': 4152,
                'longitude_origin': 0.0,
                'longitude_step': 0.08670520037412643
            },
            {
                'latitude': 17.258346557617188,
                'point_count': 4156,
                'longitude_origin': 0.0,
                'longitude_step': 0.08662175387144089
            },
            {
                'latitude': 17.188047409057617,
                'point_count': 4160,
                'longitude_origin': 0.0,
                'longitude_step': 0.08653846383094788
            },
            {
                'latitude': 17.11775016784668,
                'point_count': 4164,
                'longitude_origin': 0.0,
                'longitude_step': 0.0864553302526474
            },
            {
                'latitude': 17.04745101928711,
                'point_count': 4168,
                'longitude_origin': 0.0,
                'longitude_step': 0.08637236058712006
            },
            {
                'latitude': 16.97715187072754,
                'point_count': 4172,
                'longitude_origin': 0.0,
                'longitude_step': 0.08628954738378525
            },
            {
                'latitude': 16.90685272216797,
                'point_count': 4176,
                'longitude_origin': 0.0,
                'longitude_step': 0.08620689809322357
            },
            {
                'latitude': 16.8365535736084,
                'point_count': 4180,
                'longitude_origin': 0.0,
                'longitude_step': 0.08612440526485443
            },
            {
                'latitude': 16.76625633239746,
                'point_count': 4184,
                'longitude_origin': 0.0,
                'longitude_step': 0.08604206144809723
            },
            {
                'latitude': 16.69595718383789,
                'point_count': 4188,
                'longitude_origin': 0.0,
                'longitude_step': 0.08595988899469376
            },
            {
                'latitude': 16.62565803527832,
                'point_count': 4192,
                'longitude_origin': 0.0,
                'longitude_step': 0.08587786555290222
            },
            {
                'latitude': 16.55535888671875,
                'point_count': 4196,
                'longitude_origin': 0.0,
                'longitude_step': 0.08579599857330322
            },
            {
                'latitude': 16.48505973815918,
                'point_count': 4200,
                'longitude_origin': 0.0,
                'longitude_step': 0.08571428805589676
            },
            {
                'latitude': 16.414762496948242,
                'point_count': 4204,
                'longitude_origin': 0.0,
                'longitude_step': 0.08563273400068283
            },
            {
                'latitude': 16.344463348388672,
                'point_count': 4208,
                'longitude_origin': 0.0,
                'longitude_step': 0.08555132895708084
            },
            {
                'latitude': 16.2741641998291,
                'point_count': 4212,
                'longitude_origin': 0.0,
                'longitude_step': 0.08547008782625198
            },
            {
                'latitude': 16.20386505126953,
                'point_count': 4216,
                'longitude_origin': 0.0,
                'longitude_step': 0.08538899570703506
            },
            {
                'latitude': 16.13356590270996,
                'point_count': 4220,
                'longitude_origin': 0.0,
                'longitude_step': 0.08530806005001068
            },
            {
                'latitude': 16.063268661499023,
                'point_count': 4224,
                'longitude_origin': 0.0,
                'longitude_step': 0.08522727340459824
            },
            {
                'latitude': 15.99297046661377,
                'point_count': 4228,
                'longitude_origin': 0.0,
                'longitude_step': 0.08514664322137833
            },
            {
                'latitude': 15.9226713180542,
                'point_count': 4232,
                'longitude_origin': 0.0,
                'longitude_step': 0.08506616204977036
            },
            {
                'latitude': 15.852372169494629,
                'point_count': 4236,
                'longitude_origin': 0.0,
                'longitude_step': 0.08498583734035492
            },
            {
                'latitude': 15.782073974609375,
                'point_count': 4240,
                'longitude_origin': 0.0,
                'longitude_step': 0.08490566164255142
            },
            {
                'latitude': 15.711774826049805,
                'point_count': 4244,
                'longitude_origin': 0.0,
                'longitude_step': 0.08482563495635986
            },
            {
                'latitude': 15.64147663116455,
                'point_count': 4248,
                'longitude_origin': 0.0,
                'longitude_step': 0.08474576473236084
            },
            {
                'latitude': 15.57117748260498,
                'point_count': 4252,
                'longitude_origin': 0.0,
                'longitude_step': 0.08466603606939316
            },
            {
                'latitude': 15.50087833404541,
                'point_count': 4256,
                'longitude_origin': 0.0,
                'longitude_step': 0.08458646386861801
            },
            {
                'latitude': 15.430580139160156,
                'point_count': 4260,
                'longitude_origin': 0.0,
                'longitude_step': 0.0845070406794548
            },
            {
                'latitude': 15.360280990600586,
                'point_count': 4264,
                'longitude_origin': 0.0,
                'longitude_step': 0.08442776650190353
            },
            {
                'latitude': 15.289982795715332,
                'point_count': 4268,
                'longitude_origin': 0.0,
                'longitude_step': 0.0843486413359642
            },
            {
                'latitude': 15.219683647155762,
                'point_count': 4272,
                'longitude_origin': 0.0,
                'longitude_step': 0.08426966518163681
            },
            {
                'latitude': 15.149384498596191,
                'point_count': 4276,
                'longitude_origin': 0.0,
                'longitude_step': 0.08419083058834076
            },
            {
                'latitude': 15.079086303710938,
                'point_count': 4280,
                'longitude_origin': 0.0,
                'longitude_step': 0.08411215245723724
            },
            {
                'latitude': 15.008787155151367,
                'point_count': 4284,
                'longitude_origin': 0.0,
                'longitude_step': 0.08403361588716507
            },
            {
                'latitude': 14.938488960266113,
                'point_count': 4288,
                'longitude_origin': 0.0,
                'longitude_step': 0.08395522087812424
            },
            {
                'latitude': 14.868189811706543,
                'point_count': 4292,
                'longitude_origin': 0.0,
                'longitude_step': 0.08387698233127594
            },
            {
                'latitude': 14.797890663146973,
                'point_count': 4296,
                'longitude_origin': 0.0,
                'longitude_step': 0.08379888534545898
            },
            {
                'latitude': 14.727592468261719,
                'point_count': 4300,
                'longitude_origin': 0.0,
                'longitude_step': 0.08372092992067337
            },
            {
                'latitude': 14.657293319702148,
                'point_count': 4304,
                'longitude_origin': 0.0,
                'longitude_step': 0.0836431235074997
            },
            {
                'latitude': 14.586995124816895,
                'point_count': 4308,
                'longitude_origin': 0.0,
                'longitude_step': 0.08356545865535736
            },
            {
                'latitude': 14.516695976257324,
                'point_count': 4312,
                'longitude_origin': 0.0,
                'longitude_step': 0.08348794281482697
            },
            {
                'latitude': 14.446396827697754,
                'point_count': 4316,
                'longitude_origin': 0.0,
                'longitude_step': 0.08341056853532791
            },
            {
                'latitude': 14.3760986328125,
                'point_count': 4320,
                'longitude_origin': 0.0,
                'longitude_step': 0.0833333358168602
            },
            {
                'latitude': 14.30579948425293,
                'point_count': 4324,
                'longitude_origin': 0.0,
                'longitude_step': 0.08325624465942383
            },
            {
                'latitude': 14.235501289367676,
                'point_count': 4328,
                'longitude_origin': 0.0,
                'longitude_step': 0.0831792950630188
            },
            {
                'latitude': 14.165202140808105,
                'point_count': 4332,
                'longitude_origin': 0.0,
                'longitude_step': 0.08310249447822571
            },
            {
                'latitude': 14.094902992248535,
                'point_count': 4336,
                'longitude_origin': 0.0,
                'longitude_step': 0.08302582800388336
            },
            {
                'latitude': 14.024604797363281,
                'point_count': 4340,
                'longitude_origin': 0.0,
                'longitude_step': 0.08294931054115295
            },
            {
                'latitude': 13.954305648803711,
                'point_count': 4344,
                'longitude_origin': 0.0,
                'longitude_step': 0.08287292718887329
            },
            {
                'latitude': 13.884007453918457,
                'point_count': 4348,
                'longitude_origin': 0.0,
                'longitude_step': 0.08279668539762497
            },
            {
                'latitude': 13.813708305358887,
                'point_count': 4352,
                'longitude_origin': 0.0,
                'longitude_step': 0.08272058516740799
            },
            {
                'latitude': 13.743409156799316,
                'point_count': 4356,
                'longitude_origin': 0.0,
                'longitude_step': 0.08264462649822235
            },
            {
                'latitude': 13.673110961914062,
                'point_count': 4360,
                'longitude_origin': 0.0,
                'longitude_step': 0.08256880939006805
            },
            {
                'latitude': 13.602811813354492,
                'point_count': 4364,
                'longitude_origin': 0.0,
                'longitude_step': 0.0824931263923645
            },
            {
                'latitude': 13.532513618469238,
                'point_count': 4368,
                'longitude_origin': 0.0,
                'longitude_step': 0.08241758495569229
            },
            {
                'latitude': 13.462214469909668,
                'point_count': 4372,
                'longitude_origin': 0.0,
                'longitude_step': 0.08234217762947083
            },
            {
                'latitude': 13.391915321350098,
                'point_count': 4376,
                'longitude_origin': 0.0,
                'longitude_step': 0.0822669118642807
            },
            {
                'latitude': 13.321617126464844,
                'point_count': 4380,
                'longitude_origin': 0.0,
                'longitude_step': 0.08219178020954132
            },
            {
                'latitude': 13.251317977905273,
                'point_count': 4384,
                'longitude_origin': 0.0,
                'longitude_step': 0.08211679011583328
            },
            {
                'latitude': 13.181018829345703,
                'point_count': 4388,
                'longitude_origin': 0.0,
                'longitude_step': 0.08204193413257599
            },
            {
                'latitude': 13.11072063446045,
                'point_count': 4392,
                'longitude_origin': 0.0,
                'longitude_step': 0.08196721225976944
            },
            {
                'latitude': 13.040421485900879,
                'point_count': 4396,
                'longitude_origin': 0.0,
                'longitude_step': 0.08189263194799423
            },
            {
                'latitude': 12.970123291015625,
                'point_count': 4400,
                'longitude_origin': 0.0,
                'longitude_step': 0.08181817829608917
            },
            {
                'latitude': 12.899824142456055,
                'point_count': 4404,
                'longitude_origin': 0.0,
                'longitude_step': 0.08174386620521545
            },
            {
                'latitude': 12.829524993896484,
                'point_count': 4408,
                'longitude_origin': 0.0,
                'longitude_step': 0.08166968822479248
            },
            {
                'latitude': 12.75922679901123,
                'point_count': 4412,
                'longitude_origin': 0.0,
                'longitude_step': 0.08159565180540085
            },
            {
                'latitude': 12.68892765045166,
                'point_count': 4416,
                'longitude_origin': 0.0,
                'longitude_step': 0.08152174204587936
            },
            {
                'latitude': 12.618629455566406,
                'point_count': 4420,
                'longitude_origin': 0.0,
                'longitude_step': 0.08144796639680862
            },
            {
                'latitude': 12.548330307006836,
                'point_count': 4424,
                'longitude_origin': 0.0,
                'longitude_step': 0.08137432485818863
            },
            {
                'latitude': 12.478031158447266,
                'point_count': 4428,
                'longitude_origin': 0.0,
                'longitude_step': 0.08130080997943878
            },
            {
                'latitude': 12.407732963562012,
                'point_count': 4432,
                'longitude_origin': 0.0,
                'longitude_step': 0.08122743666172028
            },
            {
                'latitude': 12.337433815002441,
                'point_count': 4436,
                'longitude_origin': 0.0,
                'longitude_step': 0.08115419000387192
            },
            {
                'latitude': 12.267135620117188,
                'point_count': 4440,
                'longitude_origin': 0.0,
                'longitude_step': 0.0810810774564743
            },
            {
                'latitude': 12.196836471557617,
                'point_count': 4444,
                'longitude_origin': 0.0,
                'longitude_step': 0.08100809901952744
            },
            {
                'latitude': 12.126537322998047,
                'point_count': 4448,
                'longitude_origin': 0.0,
                'longitude_step': 0.08093525469303131
            },
            {
                'latitude': 12.056239128112793,
                'point_count': 4452,
                'longitude_origin': 0.0,
                'longitude_step': 0.08086253702640533
            },
            {
                'latitude': 11.985939979553223,
                'point_count': 4456,
                'longitude_origin': 0.0,
                'longitude_step': 0.0807899460196495
            },
            {
                'latitude': 11.915641784667969,
                'point_count': 4460,
                'longitude_origin': 0.0,
                'longitude_step': 0.08071748912334442
            },
            {
                'latitude': 11.845342636108398,
                'point_count': 4464,
                'longitude_origin': 0.0,
                'longitude_step': 0.08064515888690948
            },
            {
                'latitude': 11.775043487548828,
                'point_count': 4468,
                'longitude_origin': 0.0,
                'longitude_step': 0.08057296276092529
            },
            {
                'latitude': 11.704745292663574,
                'point_count': 4472,
                'longitude_origin': 0.0,
                'longitude_step': 0.08050089329481125
            },
            {
                'latitude': 11.634446144104004,
                'point_count': 4476,
                'longitude_origin': 0.0,
                'longitude_step': 0.08042895793914795
            },
            {
                'latitude': 11.56414794921875,
                'point_count': 4480,
                'longitude_origin': 0.0,
                'longitude_step': 0.0803571417927742
            },
            {
                'latitude': 11.49384880065918,
                'point_count': 4484,
                'longitude_origin': 0.0,
                'longitude_step': 0.0802854597568512
            },
            {
                'latitude': 11.42354965209961,
                'point_count': 4488,
                'longitude_origin': 0.0,
                'longitude_step': 0.08021390438079834
            },
            {
                'latitude': 11.353251457214355,
                'point_count': 4492,
                'longitude_origin': 0.0,
                'longitude_step': 0.08014247566461563
            },
            {
                'latitude': 11.282952308654785,
                'point_count': 4496,
                'longitude_origin': 0.0,
                'longitude_step': 0.08007117360830307
            },
            {
                'latitude': 11.212654113769531,
                'point_count': 4500,
                'longitude_origin': 0.0,
                'longitude_step': 0.07999999821186066
            },
            {
                'latitude': 11.142354965209961,
                'point_count': 4504,
                'longitude_origin': 0.0,
                'longitude_step': 0.07992894947528839
            },
            {
                'latitude': 11.07205581665039,
                'point_count': 4508,
                'longitude_origin': 0.0,
                'longitude_step': 0.07985802739858627
            },
            {
                'latitude': 11.001757621765137,
                'point_count': 4512,
                'longitude_origin': 0.0,
                'longitude_step': 0.0797872319817543
            },
            {
                'latitude': 10.931458473205566,
                'point_count': 4516,
                'longitude_origin': 0.0,
                'longitude_step': 0.07971656322479248
            },
            {
                'latitude': 10.861160278320312,
                'point_count': 4520,
                'longitude_origin': 0.0,
                'longitude_step': 0.0796460211277008
            },
            {
                'latitude': 10.790861129760742,
                'point_count': 4524,
                'longitude_origin': 0.0,
                'longitude_step': 0.07957559823989868
            },
            {
                'latitude': 10.720561981201172,
                'point_count': 4528,
                'longitude_origin': 0.0,
                'longitude_step': 0.0795053020119667
            },
            {
                'latitude': 10.650263786315918,
                'point_count': 4532,
                'longitude_origin': 0.0,
                'longitude_step': 0.07943512499332428
            },
            {
                'latitude': 10.579964637756348,
                'point_count': 4536,
                'longitude_origin': 0.0,
                'longitude_step': 0.0793650820851326
            },
            {
                'latitude': 10.509666442871094,
                'point_count': 4540,
                'longitude_origin': 0.0,
                'longitude_step': 0.07929515093564987
            },
            {
                'latitude': 10.439367294311523,
                'point_count': 4544,
                'longitude_origin': 0.0,
                'longitude_step': 0.07922535389661789
            },
            {
                'latitude': 10.369068145751953,
                'point_count': 4548,
                'longitude_origin': 0.0,
                'longitude_step': 0.07915567606687546
            },
            {
                'latitude': 10.2987699508667,
                'point_count': 4552,
                'longitude_origin': 0.0,
                'longitude_step': 0.07908611744642258
            },
            {
                'latitude': 10.228470802307129,
                'point_count': 4556,
                'longitude_origin': 0.0,
                'longitude_step': 0.07901667803525925
            },
            {
                'latitude': 10.158172607421875,
                'point_count': 4560,
                'longitude_origin': 0.0,
                'longitude_step': 0.07894736528396606
            },
            {
                'latitude': 10.087873458862305,
                'point_count': 4564,
                'longitude_origin': 0.0,
                'longitude_step': 0.07887817919254303
            },
            {
                'latitude': 10.017574310302734,
                'point_count': 4568,
                'longitude_origin': 0.0,
                'longitude_step': 0.07880910485982895
            },
            {
                'latitude': 9.94727611541748,
                'point_count': 4572,
                'longitude_origin': 0.0,
                'longitude_step': 0.07874015718698502
            },
            {
                'latitude': 9.87697696685791,
                'point_count': 4576,
                'longitude_origin': 0.0,
                'longitude_step': 0.07867132872343063
            },
            {
                'latitude': 9.806678771972656,
                'point_count': 4580,
                'longitude_origin': 0.0,
                'longitude_step': 0.0786026194691658
            },
            {
                'latitude': 9.736379623413086,
                'point_count': 4584,
                'longitude_origin': 0.0,
                'longitude_step': 0.07853402942419052
            },
            {
                'latitude': 9.666080474853516,
                'point_count': 4588,
                'longitude_origin': 0.0,
                'longitude_step': 0.07846556603908539
            },
            {
                'latitude': 9.595782279968262,
                'point_count': 4592,
                'longitude_origin': 0.0,
                'longitude_step': 0.07839721441268921
            },
            {
                'latitude': 9.525483131408691,
                'point_count': 4596,
                'longitude_origin': 0.0,
                'longitude_step': 0.07832898199558258
            },
            {
                'latitude': 9.455184936523438,
                'point_count': 4600,
                'longitude_origin': 0.0,
                'longitude_step': 0.0782608687877655
            },
            {
                'latitude': 9.384885787963867,
                'point_count': 4604,
                'longitude_origin': 0.0,
                'longitude_step': 0.07819287478923798
            },
            {
                'latitude': 9.314586639404297,
                'point_count': 4608,
                'longitude_origin': 0.0,
                'longitude_step': 0.078125
            },
            {
                'latitude': 9.244288444519043,
                'point_count': 4612,
                'longitude_origin': 0.0,
                'longitude_step': 0.07805724442005157
            },
            {
                'latitude': 9.173989295959473,
                'point_count': 4616,
                'longitude_origin': 0.0,
                'longitude_step': 0.0779896005988121
            },
            {
                'latitude': 9.103691101074219,
                'point_count': 4620,
                'longitude_origin': 0.0,
                'longitude_step': 0.07792207598686218
            },
            {
                'latitude': 9.033391952514648,
                'point_count': 4624,
                'longitude_origin': 0.0,
                'longitude_step': 0.07785467058420181
            },
            {
                'latitude': 8.963092803955078,
                'point_count': 4628,
                'longitude_origin': 0.0,
                'longitude_step': 0.077787384390831
            },
            {
                'latitude': 8.892794609069824,
                'point_count': 4632,
                'longitude_origin': 0.0,
                'longitude_step': 0.07772020995616913
            },
            {
                'latitude': 8.822495460510254,
                'point_count': 4636,
                'longitude_origin': 0.0,
                'longitude_step': 0.07765314728021622
            },
            {
                'latitude': 8.752197265625,
                'point_count': 4640,
                'longitude_origin': 0.0,
                'longitude_step': 0.07758620381355286
            },
            {
                'latitude': 8.68189811706543,
                'point_count': 4644,
                'longitude_origin': 0.0,
                'longitude_step': 0.07751937955617905
            },
            {
                'latitude': 8.61159896850586,
                'point_count': 4648,
                'longitude_origin': 0.0,
                'longitude_step': 0.07745266705751419
            },
            {
                'latitude': 8.541300773620605,
                'point_count': 4652,
                'longitude_origin': 0.0,
                'longitude_step': 0.07738607376813889
            },
            {
                'latitude': 8.471001625061035,
                'point_count': 4656,
                'longitude_origin': 0.0,
                'longitude_step': 0.07731958478689194
            },
            {
                'latitude': 8.400703430175781,
                'point_count': 4660,
                'longitude_origin': 0.0,
                'longitude_step': 0.07725322246551514
            },
            {
                'latitude': 8.330404281616211,
                'point_count': 4664,
                'longitude_origin': 0.0,
                'longitude_step': 0.0771869644522667
            },
            {
                'latitude': 8.26010513305664,
                'point_count': 4668,
                'longitude_origin': 0.0,
                'longitude_step': 0.0771208256483078
            },
            {
                'latitude': 8.189806938171387,
                'point_count': 4672,
                'longitude_origin': 0.0,
                'longitude_step': 0.07705479115247726
            },
            {
                'latitude': 8.119507789611816,
                'point_count': 4676,
                'longitude_origin': 0.0,
                'longitude_step': 0.07698887586593628
            },
            {
                'latitude': 8.049209594726562,
                'point_count': 4680,
                'longitude_origin': 0.0,
                'longitude_step': 0.07692307978868484
            },
            {
                'latitude': 7.978910446166992,
                'point_count': 4684,
                'longitude_origin': 0.0,
                'longitude_step': 0.07685738801956177
            },
            {
                'latitude': 7.90861177444458,
                'point_count': 4688,
                'longitude_origin': 0.0,
                'longitude_step': 0.07679180800914764
            },
            {
                'latitude': 7.838313102722168,
                'point_count': 4692,
                'longitude_origin': 0.0,
                'longitude_step': 0.07672633975744247
            },
            {
                'latitude': 7.768013954162598,
                'point_count': 4696,
                'longitude_origin': 0.0,
                'longitude_step': 0.07666099071502686
            },
            {
                'latitude': 7.6977152824401855,
                'point_count': 4700,
                'longitude_origin': 0.0,
                'longitude_step': 0.0765957459807396
            },
            {
                'latitude': 7.627416610717773,
                'point_count': 4704,
                'longitude_origin': 0.0,
                'longitude_step': 0.07653061300516129
            },
            {
                'latitude': 7.557117938995361,
                'point_count': 4708,
                'longitude_origin': 0.0,
                'longitude_step': 0.07646559178829193
            },
            {
                'latitude': 7.486819267272949,
                'point_count': 4712,
                'longitude_origin': 0.0,
                'longitude_step': 0.07640068233013153
            },
            {
                'latitude': 7.416520118713379,
                'point_count': 4716,
                'longitude_origin': 0.0,
                'longitude_step': 0.07633587718009949
            },
            {
                'latitude': 7.346221446990967,
                'point_count': 4720,
                'longitude_origin': 0.0,
                'longitude_step': 0.0762711837887764
            },
            {
                'latitude': 7.275922775268555,
                'point_count': 4724,
                'longitude_origin': 0.0,
                'longitude_step': 0.07620660215616226
            },
            {
                'latitude': 7.205624103546143,
                'point_count': 4728,
                'longitude_origin': 0.0,
                'longitude_step': 0.07614213228225708
            },
            {
                'latitude': 7.1353254318237305,
                'point_count': 4732,
                'longitude_origin': 0.0,
                'longitude_step': 0.07607776671648026
            },
            {
                'latitude': 7.06502628326416,
                'point_count': 4736,
                'longitude_origin': 0.0,
                'longitude_step': 0.07601351290941238
            },
            {
                'latitude': 6.994727611541748,
                'point_count': 4740,
                'longitude_origin': 0.0,
                'longitude_step': 0.07594936341047287
            },
            {
                'latitude': 6.924428939819336,
                'point_count': 4744,
                'longitude_origin': 0.0,
                'longitude_step': 0.07588532567024231
            },
            {
                'latitude': 6.854130268096924,
                'point_count': 4748,
                'longitude_origin': 0.0,
                'longitude_step': 0.0758213996887207
            },
            {
                'latitude': 6.783831596374512,
                'point_count': 4752,
                'longitude_origin': 0.0,
                'longitude_step': 0.07575757801532745
            },
            {
                'latitude': 6.713532447814941,
                'point_count': 4756,
                'longitude_origin': 0.0,
                'longitude_step': 0.07569386065006256
            },
            {
                'latitude': 6.643233776092529,
                'point_count': 4760,
                'longitude_origin': 0.0,
                'longitude_step': 0.07563025504350662
            },
            {
                'latitude': 6.572935104370117,
                'point_count': 4764,
                'longitude_origin': 0.0,
                'longitude_step': 0.07556675374507904
            },
            {
                'latitude': 6.502636432647705,
                'point_count': 4768,
                'longitude_origin': 0.0,
                'longitude_step': 0.07550335675477982
            },
            {
                'latitude': 6.432337284088135,
                'point_count': 4772,
                'longitude_origin': 0.0,
                'longitude_step': 0.07544006407260895
            },
            {
                'latitude': 6.362038612365723,
                'point_count': 4776,
                'longitude_origin': 0.0,
                'longitude_step': 0.07537688314914703
            },
            {
                'latitude': 6.2917399406433105,
                'point_count': 4780,
                'longitude_origin': 0.0,
                'longitude_step': 0.07531380653381348
            },
            {
                'latitude': 6.221441268920898,
                'point_count': 4784,
                'longitude_origin': 0.0,
                'longitude_step': 0.07525083422660828
            },
            {
                'latitude': 6.151142597198486,
                'point_count': 4788,
                'longitude_origin': 0.0,
                'longitude_step': 0.07518796622753143
            },
            {
                'latitude': 6.080843448638916,
                'point_count': 4792,
                'longitude_origin': 0.0,
                'longitude_step': 0.07512520998716354
            },
            {
                'latitude': 6.010544776916504,
                'point_count': 4796,
                'longitude_origin': 0.0,
                'longitude_step': 0.07506255060434341
            },
            {
                'latitude': 5.940246105194092,
                'point_count': 4800,
                'longitude_origin': 0.0,
                'longitude_step': 0.07500000298023224
            },
            {
                'latitude': 5.86994743347168,
                'point_count': 4804,
                'longitude_origin': 0.0,
                'longitude_step': 0.07493755221366882
            },
            {
                'latitude': 5.799648761749268,
                'point_count': 4808,
                'longitude_origin': 0.0,
                'longitude_step': 0.07487520575523376
            },
            {
                'latitude': 5.729349613189697,
                'point_count': 4812,
                'longitude_origin': 0.0,
                'longitude_step': 0.07481297105550766
            },
            {
                'latitude': 5.659050941467285,
                'point_count': 4816,
                'longitude_origin': 0.0,
                'longitude_step': 0.07475083321332932
            },
            {
                'latitude': 5.588752269744873,
                'point_count': 4820,
                'longitude_origin': 0.0,
                'longitude_step': 0.07468879967927933
            },
            {
                'latitude': 5.518453598022461,
                'point_count': 4824,
                'longitude_origin': 0.0,
                'longitude_step': 0.0746268630027771
            },
            {
                'latitude': 5.448154926300049,
                'point_count': 4828,
                'longitude_origin': 0.0,
                'longitude_step': 0.07456503808498383
            },
            {
                'latitude': 5.3778557777404785,
                'point_count': 4832,
                'longitude_origin': 0.0,
                'longitude_step': 0.07450331002473831
            },
            {
                'latitude': 5.307557106018066,
                'point_count': 4836,
                'longitude_origin': 0.0,
                'longitude_step': 0.07444168627262115
            },
            {
                'latitude': 5.237258434295654,
                'point_count': 4840,
                'longitude_origin': 0.0,
                'longitude_step': 0.07438016682863235
            },
            {
                'latitude': 5.166959762573242,
                'point_count': 4844,
                'longitude_origin': 0.0,
                'longitude_step': 0.07431874424219131
            },
            {
                'latitude': 5.09666109085083,
                'point_count': 4848,
                'longitude_origin': 0.0,
                'longitude_step': 0.07425742596387863
            },
            {
                'latitude': 5.02636194229126,
                'point_count': 4852,
                'longitude_origin': 0.0,
                'longitude_step': 0.07419620454311371
            },
            {
                'latitude': 4.956063270568848,
                'point_count': 4856,
                'longitude_origin': 0.0,
                'longitude_step': 0.07413508743047714
            },
            {
                'latitude': 4.8857645988464355,
                'point_count': 4860,
                'longitude_origin': 0.0,
                'longitude_step': 0.07407407462596893
            },
            {
                'latitude': 4.815465927124023,
                'point_count': 4864,
                'longitude_origin': 0.0,
                'longitude_step': 0.07401315867900848
            },
            {
                'latitude': 4.745167255401611,
                'point_count': 4868,
                'longitude_origin': 0.0,
                'longitude_step': 0.0739523395895958
            },
            {
                'latitude': 4.674868106842041,
                'point_count': 4872,
                'longitude_origin': 0.0,
                'longitude_step': 0.07389162480831146
            },
            {
                'latitude': 4.604569435119629,
                'point_count': 4876,
                'longitude_origin': 0.0,
                'longitude_step': 0.07383100688457489
            },
            {
                'latitude': 4.534270763397217,
                'point_count': 4880,
                'longitude_origin': 0.0,
                'longitude_step': 0.07377049326896667
            },
            {
                'latitude': 4.463972091674805,
                'point_count': 4884,
                'longitude_origin': 0.0,
                'longitude_step': 0.07371007651090622
            },
            {
                'latitude': 4.393673419952393,
                'point_count': 4888,
                'longitude_origin': 0.0,
                'longitude_step': 0.07364975661039352
            },
            {
                'latitude': 4.323374271392822,
                'point_count': 4892,
                'longitude_origin': 0.0,
                'longitude_step': 0.07358953356742859
            },
            {
                'latitude': 4.25307559967041,
                'point_count': 4896,
                'longitude_origin': 0.0,
                'longitude_step': 0.07352941483259201
            },
            {
                'latitude': 4.182776927947998,
                'point_count': 4900,
                'longitude_origin': 0.0,
                'longitude_step': 0.0734693855047226
            },
            {
                'latitude': 4.112478256225586,
                'point_count': 4904,
                'longitude_origin': 0.0,
                'longitude_step': 0.07340946048498154
            },
            {
                'latitude': 4.042179584503174,
                'point_count': 4908,
                'longitude_origin': 0.0,
                'longitude_step': 0.07334963232278824
            },
            {
                'latitude': 3.9718804359436035,
                'point_count': 4912,
                'longitude_origin': 0.0,
                'longitude_step': 0.0732899010181427
            },
            {
                'latitude': 3.9015815258026123,
                'point_count': 4916,
                'longitude_origin': 0.0,
                'longitude_step': 0.07323026657104492
            },
            {
                'latitude': 3.8312828540802,
                'point_count': 4920,
                'longitude_origin': 0.0,
                'longitude_step': 0.0731707289814949
            },
            {
                'latitude': 3.760984182357788,
                'point_count': 4924,
                'longitude_origin': 0.0,
                'longitude_step': 0.07311128824949265
            },
            {
                'latitude': 3.690685272216797,
                'point_count': 4928,
                'longitude_origin': 0.0,
                'longitude_step': 0.07305194437503815
            },
            {
                'latitude': 3.6203866004943848,
                'point_count': 4932,
                'longitude_origin': 0.0,
                'longitude_step': 0.07299269735813141
            },
            {
                'latitude': 3.5500876903533936,
                'point_count': 4936,
                'longitude_origin': 0.0,
                'longitude_step': 0.07293354719877243
            },
            {
                'latitude': 3.4797890186309814,
                'point_count': 4940,
                'longitude_origin': 0.0,
                'longitude_step': 0.07287449389696121
            },
            {
                'latitude': 3.4094903469085693,
                'point_count': 4944,
                'longitude_origin': 0.0,
                'longitude_step': 0.07281553745269775
            },
            {
                'latitude': 3.339191436767578,
                'point_count': 4948,
                'longitude_origin': 0.0,
                'longitude_step': 0.07275667041540146
            },
            {
                'latitude': 3.268892765045166,
                'point_count': 4952,
                'longitude_origin': 0.0,
                'longitude_step': 0.07269790023565292
            },
            {
                'latitude': 3.198593854904175,
                'point_count': 4956,
                'longitude_origin': 0.0,
                'longitude_step': 0.07263922691345215
            },
            {
                'latitude': 3.1282951831817627,
                'point_count': 4960,
                'longitude_origin': 0.0,
                'longitude_step': 0.07258064299821854
            },
            {
                'latitude': 3.0579962730407715,
                'point_count': 4964,
                'longitude_origin': 0.0,
                'longitude_step': 0.07252215594053268
            },
            {
                'latitude': 2.9876976013183594,
                'point_count': 4968,
                'longitude_origin': 0.0,
                'longitude_step': 0.07246376574039459
            },
            {
                'latitude': 2.9173989295959473,
                'point_count': 4972,
                'longitude_origin': 0.0,
                'longitude_step': 0.07240547239780426
            },
            {
                'latitude': 2.847100019454956,
                'point_count': 4976,
                'longitude_origin': 0.0,
                'longitude_step': 0.07234726846218109
            },
            {
                'latitude': 2.776801347732544,
                'point_count': 4980,
                'longitude_origin': 0.0,
                'longitude_step': 0.07228915393352509
            },
            {
                'latitude': 2.7065024375915527,
                'point_count': 4984,
                'longitude_origin': 0.0,
                'longitude_step': 0.07223113626241684
            },
            {
                'latitude': 2.6362037658691406,
                'point_count': 4988,
                'longitude_origin': 0.0,
                'longitude_step': 0.07217321544885635
            },
            {
                'latitude': 2.5659050941467285,
                'point_count': 4992,
                'longitude_origin': 0.0,
                'longitude_step': 0.07211538404226303
            },
            {
                'latitude': 2.4956061840057373,
                'point_count': 4996,
                'longitude_origin': 0.0,
                'longitude_step': 0.07205764949321747
            },
            {
                'latitude': 2.425307512283325,
                'point_count': 5000,
                'longitude_origin': 0.0,
                'longitude_step': 0.07199999690055847
            },
            {
                'latitude': 2.355008602142334,
                'point_count': 5004,
                'longitude_origin': 0.0,
                'longitude_step': 0.07194244861602783
            },
            {
                'latitude': 2.284709930419922,
                'point_count': 5008,
                'longitude_origin': 0.0,
                'longitude_step': 0.07188498228788376
            },
            {
                'latitude': 2.2144112586975098,
                'point_count': 5012,
                'longitude_origin': 0.0,
                'longitude_step': 0.07182761281728745
            },
            {
                'latitude': 2.1441123485565186,
                'point_count': 5016,
                'longitude_origin': 0.0,
                'longitude_step': 0.0717703327536583
            },
            {
                'latitude': 2.0738136768341064,
                'point_count': 5020,
                'longitude_origin': 0.0,
                'longitude_step': 0.0717131495475769
            },
            {
                'latitude': 2.0035150051116943,
                'point_count': 5024,
                'longitude_origin': 0.0,
                'longitude_step': 0.07165604829788208
            },
            {
                'latitude': 1.9332160949707031,
                'point_count': 5028,
                'longitude_origin': 0.0,
                'longitude_step': 0.07159904390573502
            },
            {
                'latitude': 1.8629173040390015,
                'point_count': 5032,
                'longitude_origin': 0.0,
                'longitude_step': 0.07154212892055511
            },
            {
                'latitude': 1.7926185131072998,
                'point_count': 5036,
                'longitude_origin': 0.0,
                'longitude_step': 0.07148530334234238
            },
            {
                'latitude': 1.7223198413848877,
                'point_count': 5040,
                'longitude_origin': 0.0,
                'longitude_step': 0.0714285746216774
            },
            {
                'latitude': 1.652021050453186,
                'point_count': 5044,
                'longitude_origin': 0.0,
                'longitude_step': 0.07137192785739899
            },
            {
                'latitude': 1.5817222595214844,
                'point_count': 5048,
                'longitude_origin': 0.0,
                'longitude_step': 0.07131537050008774
            },
            {
                'latitude': 1.5114234685897827,
                'point_count': 5052,
                'longitude_origin': 0.0,
                'longitude_step': 0.07125891000032425
            },
            {
                'latitude': 1.441124677658081,
                'point_count': 5056,
                'longitude_origin': 0.0,
                'longitude_step': 0.07120253145694733
            },
            {
                'latitude': 1.3708258867263794,
                'point_count': 5060,
                'longitude_origin': 0.0,
                'longitude_step': 0.07114624232053757
            },
            {
                'latitude': 1.3005272150039673,
                'point_count': 5064,
                'longitude_origin': 0.0,
                'longitude_step': 0.07109005004167557
            },
            {
                'latitude': 1.2302284240722656,
                'point_count': 5068,
                'longitude_origin': 0.0,
                'longitude_step': 0.07103393971920013
            },
            {
                'latitude': 1.159929633140564,
                'point_count': 5072,
                'longitude_origin': 0.0,
                'longitude_step': 0.07097791880369186
            },
            {
                'latitude': 1.0896308422088623,
                'point_count': 5076,
                'longitude_origin': 0.0,
                'longitude_step': 0.07092198729515076
            },
            {
                'latitude': 1.0193321704864502,
                'point_count': 5080,
                'longitude_origin': 0.0,
                'longitude_step': 0.07086614519357681
            },
            {
                'latitude': 0.9490333795547485,
                'point_count': 5084,
                'longitude_origin': 0.0,
                'longitude_step': 0.07081038504838943
            },
            {
                'latitude': 0.8787346482276917,
                'point_count': 5088,
                'longitude_origin': 0.0,
                'longitude_step': 0.07075471431016922
            },
            {
                'latitude': 0.80843585729599,
                'point_count': 5092,
                'longitude_origin': 0.0,
                'longitude_step': 0.07069913297891617
            },
            {
                'latitude': 0.7381370663642883,
                'point_count': 5096,
                'longitude_origin': 0.0,
                'longitude_step': 0.07064364105463028
            },
            {
                'latitude': 0.6678383350372314,
                'point_count': 5100,
                'longitude_origin': 0.0,
                'longitude_step': 0.07058823853731155
            },
            {
                'latitude': 0.5975395441055298,
                'point_count': 5104,
                'longitude_origin': 0.0,
                'longitude_step': 0.0705329179763794
            },
            {
                'latitude': 0.5272407531738281,
                'point_count': 5108,
                'longitude_origin': 0.0,
                'longitude_step': 0.0704776793718338
            },
            {
                'latitude': 0.45694202184677124,
                'point_count': 5112,
                'longitude_origin': 0.0,
                'longitude_step': 0.07042253762483597
            },
            {
                'latitude': 0.3866432309150696,
                'point_count': 5116,
                'longitude_origin': 0.0,
                'longitude_step': 0.0703674778342247
            },
            {
                'latitude': 0.3163444697856903,
                'point_count': 5120,
                'longitude_origin': 0.0,
                'longitude_step': 0.0703125
            },
            {
                'latitude': 0.24604569375514984,
                'point_count': 5124,
                'longitude_origin': 0.0,
                'longitude_step': 0.07025761157274246
            },
            {
                'latitude': 0.17574691772460938,
                'point_count': 5128,
                'longitude_origin': 0.0,
                'longitude_step': 0.07020280510187149
            },
            {
                'latitude': 0.1054481565952301,
                'point_count': 5132,
                'longitude_origin': 0.0,
                'longitude_step': 0.07014808803796768
            },
            {
                'latitude': 0.035149384289979935,
                'point_count': 5136,
                'longitude_origin': 0.0,
                'longitude_step': 0.07009346038103104
            },
            {
                'latitude': -0.035149384289979935,
                'point_count': 5136,
                'longitude_origin': 0.0,
                'longitude_step': 0.07009346038103104
            },
            {
                'latitude': -0.1054481565952301,
                'point_count': 5132,
                'longitude_origin': 0.0,
                'longitude_step': 0.07014808803796768
            },
            {
                'latitude': -0.17574693262577057,
                'point_count': 5128,
                'longitude_origin': 0.0,
                'longitude_step': 0.07020280510187149
            },
            {
                'latitude': -0.24604569375514984,
                'point_count': 5124,
                'longitude_origin': 0.0,
                'longitude_step': 0.07025761157274246
            },
            {
                'latitude': -0.3163444399833679,
                'point_count': 5120,
                'longitude_origin': 0.0,
                'longitude_step': 0.0703125
            },
            {
                'latitude': -0.3866432309150696,
                'point_count': 5116,
                'longitude_origin': 0.0,
                'longitude_step': 0.0703674778342247
            },
            {
                'latitude': -0.45694199204444885,
                'point_count': 5112,
                'longitude_origin': 0.0,
                'longitude_step': 0.07042253762483597
            },
            {
                'latitude': -0.5272407531738281,
                'point_count': 5108,
                'longitude_origin': 0.0,
                'longitude_step': 0.0704776793718338
            },
            {
                'latitude': -0.5975395441055298,
                'point_count': 5104,
                'longitude_origin': 0.0,
                'longitude_step': 0.0705329179763794
            },
            {
                'latitude': -0.6678382754325867,
                'point_count': 5100,
                'longitude_origin': 0.0,
                'longitude_step': 0.07058823853731155
            },
            {
                'latitude': -0.7381370663642883,
                'point_count': 5096,
                'longitude_origin': 0.0,
                'longitude_step': 0.07064364105463028
            },
            {
                'latitude': -0.80843585729599,
                'point_count': 5092,
                'longitude_origin': 0.0,
                'longitude_step': 0.07069913297891617
            },
            {
                'latitude': -0.8787345886230469,
                'point_count': 5088,
                'longitude_origin': 0.0,
                'longitude_step': 0.07075471431016922
            },
            {
                'latitude': -0.9490333795547485,
                'point_count': 5084,
                'longitude_origin': 0.0,
                'longitude_step': 0.07081038504838943
            },
            {
                'latitude': -1.0193321704864502,
                'point_count': 5080,
                'longitude_origin': 0.0,
                'longitude_step': 0.07086614519357681
            },
            {
                'latitude': -1.0896309614181519,
                'point_count': 5076,
                'longitude_origin': 0.0,
                'longitude_step': 0.07092198729515076
            },
            {
                'latitude': -1.1599297523498535,
                'point_count': 5072,
                'longitude_origin': 0.0,
                'longitude_step': 0.07097791880369186
            },
            {
                'latitude': -1.2302285432815552,
                'point_count': 5068,
                'longitude_origin': 0.0,
                'longitude_step': 0.07103393971920013
            },
            {
                'latitude': -1.3005272150039673,
                'point_count': 5064,
                'longitude_origin': 0.0,
                'longitude_step': 0.07109005004167557
            },
            {
                'latitude': -1.370826005935669,
                'point_count': 5060,
                'longitude_origin': 0.0,
                'longitude_step': 0.07114624232053757
            },
            {
                'latitude': -1.4411247968673706,
                'point_count': 5056,
                'longitude_origin': 0.0,
                'longitude_step': 0.07120253145694733
            },
            {
                'latitude': -1.5114235877990723,
                'point_count': 5052,
                'longitude_origin': 0.0,
                'longitude_step': 0.07125891000032425
            },
            {
                'latitude': -1.581722378730774,
                'point_count': 5048,
                'longitude_origin': 0.0,
                'longitude_step': 0.07131537050008774
            },
            {
                'latitude': -1.6520211696624756,
                'point_count': 5044,
                'longitude_origin': 0.0,
                'longitude_step': 0.07137192785739899
            },
            {
                'latitude': -1.7223198413848877,
                'point_count': 5040,
                'longitude_origin': 0.0,
                'longitude_step': 0.0714285746216774
            },
            {
                'latitude': -1.7926186323165894,
                'point_count': 5036,
                'longitude_origin': 0.0,
                'longitude_step': 0.07148530334234238
            },
            {
                'latitude': -1.862917423248291,
                'point_count': 5032,
                'longitude_origin': 0.0,
                'longitude_step': 0.07154212892055511
            },
            {
                'latitude': -1.9332162141799927,
                'point_count': 5028,
                'longitude_origin': 0.0,
                'longitude_step': 0.07159904390573502
            },
            {
                'latitude': -2.0035150051116943,
                'point_count': 5024,
                'longitude_origin': 0.0,
                'longitude_step': 0.07165604829788208
            },
            {
                'latitude': -2.0738136768341064,
                'point_count': 5020,
                'longitude_origin': 0.0,
                'longitude_step': 0.0717131495475769
            },
            {
                'latitude': -2.1441125869750977,
                'point_count': 5016,
                'longitude_origin': 0.0,
                'longitude_step': 0.0717703327536583
            },
            {
                'latitude': -2.2144112586975098,
                'point_count': 5012,
                'longitude_origin': 0.0,
                'longitude_step': 0.07182761281728745
            },
            {
                'latitude': -2.284709930419922,
                'point_count': 5008,
                'longitude_origin': 0.0,
                'longitude_step': 0.07188498228788376
            },
            {
                'latitude': -2.355008840560913,
                'point_count': 5004,
                'longitude_origin': 0.0,
                'longitude_step': 0.07194244861602783
            },
            {
                'latitude': -2.425307512283325,
                'point_count': 5000,
                'longitude_origin': 0.0,
                'longitude_step': 0.07199999690055847
            },
            {
                'latitude': -2.4956064224243164,
                'point_count': 4996,
                'longitude_origin': 0.0,
                'longitude_step': 0.07205764949321747
            },
            {
                'latitude': -2.5659050941467285,
                'point_count': 4992,
                'longitude_origin': 0.0,
                'longitude_step': 0.07211538404226303
            },
            {
                'latitude': -2.6362037658691406,
                'point_count': 4988,
                'longitude_origin': 0.0,
                'longitude_step': 0.07217321544885635
            },
            {
                'latitude': -2.706502676010132,
                'point_count': 4984,
                'longitude_origin': 0.0,
                'longitude_step': 0.07223113626241684
            },
            {
                'latitude': -2.776801347732544,
                'point_count': 4980,
                'longitude_origin': 0.0,
                'longitude_step': 0.07228915393352509
            },
            {
                'latitude': -2.847100257873535,
                'point_count': 4976,
                'longitude_origin': 0.0,
                'longitude_step': 0.07234726846218109
            },
            {
                'latitude': -2.9173989295959473,
                'point_count': 4972,
                'longitude_origin': 0.0,
                'longitude_step': 0.07240547239780426
            },
            {
                'latitude': -2.9876976013183594,
                'point_count': 4968,
                'longitude_origin': 0.0,
                'longitude_step': 0.07246376574039459
            },
            {
                'latitude': -3.0579965114593506,
                'point_count': 4964,
                'longitude_origin': 0.0,
                'longitude_step': 0.07252215594053268
            },
            {
                'latitude': -3.1282951831817627,
                'point_count': 4960,
                'longitude_origin': 0.0,
                'longitude_step': 0.07258064299821854
            },
            {
                'latitude': -3.198594093322754,
                'point_count': 4956,
                'longitude_origin': 0.0,
                'longitude_step': 0.07263922691345215
            },
            {
                'latitude': -3.268892765045166,
                'point_count': 4952,
                'longitude_origin': 0.0,
                'longitude_step': 0.07269790023565292
            },
            {
                'latitude': -3.3391916751861572,
                'point_count': 4948,
                'longitude_origin': 0.0,
                'longitude_step': 0.07275667041540146
            },
            {
                'latitude': -3.4094903469085693,
                'point_count': 4944,
                'longitude_origin': 0.0,
                'longitude_step': 0.07281553745269775
            },
            {
                'latitude': -3.4797890186309814,
                'point_count': 4940,
                'longitude_origin': 0.0,
                'longitude_step': 0.07287449389696121
            },
            {
                'latitude': -3.5500879287719727,
                'point_count': 4936,
                'longitude_origin': 0.0,
                'longitude_step': 0.07293354719877243
            },
            {
                'latitude': -3.6203866004943848,
                'point_count': 4932,
                'longitude_origin': 0.0,
                'longitude_step': 0.07299269735813141
            },
            {
                'latitude': -3.690685510635376,
                'point_count': 4928,
                'longitude_origin': 0.0,
                'longitude_step': 0.07305194437503815
            },
            {
                'latitude': -3.760984182357788,
                'point_count': 4924,
                'longitude_origin': 0.0,
                'longitude_step': 0.07311128824949265
            },
            {
                'latitude': -3.8312828540802,
                'point_count': 4920,
                'longitude_origin': 0.0,
                'longitude_step': 0.0731707289814949
            },
            {
                'latitude': -3.9015817642211914,
                'point_count': 4916,
                'longitude_origin': 0.0,
                'longitude_step': 0.07323026657104492
            },
            {
                'latitude': -3.9718806743621826,
                'point_count': 4912,
                'longitude_origin': 0.0,
                'longitude_step': 0.0732899010181427
            },
            {
                'latitude': -4.042179107666016,
                'point_count': 4908,
                'longitude_origin': 0.0,
                'longitude_step': 0.07334963232278824
            },
            {
                'latitude': -4.112477779388428,
                'point_count': 4904,
                'longitude_origin': 0.0,
                'longitude_step': 0.07340946048498154
            },
            {
                'latitude': -4.18277645111084,
                'point_count': 4900,
                'longitude_origin': 0.0,
                'longitude_step': 0.0734693855047226
            },
            {
                'latitude': -4.253075122833252,
                'point_count': 4896,
                'longitude_origin': 0.0,
                'longitude_step': 0.07352941483259201
            },
            {
                'latitude': -4.323374271392822,
                'point_count': 4892,
                'longitude_origin': 0.0,
                'longitude_step': 0.07358953356742859
            },
            {
                'latitude': -4.393672943115234,
                'point_count': 4888,
                'longitude_origin': 0.0,
                'longitude_step': 0.07364975661039352
            },
            {
                'latitude': -4.4639716148376465,
                'point_count': 4884,
                'longitude_origin': 0.0,
                'longitude_step': 0.07371007651090622
            },
            {
                'latitude': -4.534270286560059,
                'point_count': 4880,
                'longitude_origin': 0.0,
                'longitude_step': 0.07377049326896667
            },
            {
                'latitude': -4.604568958282471,
                'point_count': 4876,
                'longitude_origin': 0.0,
                'longitude_step': 0.07383100688457489
            },
            {
                'latitude': -4.674868106842041,
                'point_count': 4872,
                'longitude_origin': 0.0,
                'longitude_step': 0.07389162480831146
            },
            {
                'latitude': -4.745166778564453,
                'point_count': 4868,
                'longitude_origin': 0.0,
                'longitude_step': 0.0739523395895958
            },
            {
                'latitude': -4.815465450286865,
                'point_count': 4864,
                'longitude_origin': 0.0,
                'longitude_step': 0.07401315867900848
            },
            {
                'latitude': -4.885764122009277,
                'point_count': 4860,
                'longitude_origin': 0.0,
                'longitude_step': 0.07407407462596893
            },
            {
                'latitude': -4.9560627937316895,
                'point_count': 4856,
                'longitude_origin': 0.0,
                'longitude_step': 0.07413508743047714
            },
            {
                'latitude': -5.02636194229126,
                'point_count': 4852,
                'longitude_origin': 0.0,
                'longitude_step': 0.07419620454311371
            },
            {
                'latitude': -5.096660614013672,
                'point_count': 4848,
                'longitude_origin': 0.0,
                'longitude_step': 0.07425742596387863
            },
            {
                'latitude': -5.166959285736084,
                'point_count': 4844,
                'longitude_origin': 0.0,
                'longitude_step': 0.07431874424219131
            },
            {
                'latitude': -5.237257957458496,
                'point_count': 4840,
                'longitude_origin': 0.0,
                'longitude_step': 0.07438016682863235
            },
            {
                'latitude': -5.307556629180908,
                'point_count': 4836,
                'longitude_origin': 0.0,
                'longitude_step': 0.07444168627262115
            },
            {
                'latitude': -5.3778557777404785,
                'point_count': 4832,
                'longitude_origin': 0.0,
                'longitude_step': 0.07450331002473831
            },
            {
                'latitude': -5.448154449462891,
                'point_count': 4828,
                'longitude_origin': 0.0,
                'longitude_step': 0.07456503808498383
            },
            {
                'latitude': -5.518453121185303,
                'point_count': 4824,
                'longitude_origin': 0.0,
                'longitude_step': 0.0746268630027771
            },
            {
                'latitude': -5.588751792907715,
                'point_count': 4820,
                'longitude_origin': 0.0,
                'longitude_step': 0.07468879967927933
            },
            {
                'latitude': -5.659050464630127,
                'point_count': 4816,
                'longitude_origin': 0.0,
                'longitude_step': 0.07475083321332932
            },
            {
                'latitude': -5.729349613189697,
                'point_count': 4812,
                'longitude_origin': 0.0,
                'longitude_step': 0.07481297105550766
            },
            {
                'latitude': -5.799648284912109,
                'point_count': 4808,
                'longitude_origin': 0.0,
                'longitude_step': 0.07487520575523376
            },
            {
                'latitude': -5.8699469566345215,
                'point_count': 4804,
                'longitude_origin': 0.0,
                'longitude_step': 0.07493755221366882
            },
            {
                'latitude': -5.940245628356934,
                'point_count': 4800,
                'longitude_origin': 0.0,
                'longitude_step': 0.07500000298023224
            },
            {
                'latitude': -6.010544300079346,
                'point_count': 4796,
                'longitude_origin': 0.0,
                'longitude_step': 0.07506255060434341
            },
            {
                'latitude': -6.080843448638916,
                'point_count': 4792,
                'longitude_origin': 0.0,
                'longitude_step': 0.07512520998716354
            },
            {
                'latitude': -6.151142120361328,
                'point_count': 4788,
                'longitude_origin': 0.0,
                'longitude_step': 0.07518796622753143
            },
            {
                'latitude': -6.22144079208374,
                'point_count': 4784,
                'longitude_origin': 0.0,
                'longitude_step': 0.07525083422660828
            },
            {
                'latitude': -6.291739463806152,
                'point_count': 4780,
                'longitude_origin': 0.0,
                'longitude_step': 0.07531380653381348
            },
            {
                'latitude': -6.3620381355285645,
                'point_count': 4776,
                'longitude_origin': 0.0,
                'longitude_step': 0.07537688314914703
            },
            {
                'latitude': -6.432337284088135,
                'point_count': 4772,
                'longitude_origin': 0.0,
                'longitude_step': 0.07544006407260895
            },
            {
                'latitude': -6.502635955810547,
                'point_count': 4768,
                'longitude_origin': 0.0,
                'longitude_step': 0.07550335675477982
            },
            {
                'latitude': -6.572934627532959,
                'point_count': 4764,
                'longitude_origin': 0.0,
                'longitude_step': 0.07556675374507904
            },
            {
                'latitude': -6.643233299255371,
                'point_count': 4760,
                'longitude_origin': 0.0,
                'longitude_step': 0.07563025504350662
            },
            {
                'latitude': -6.713532447814941,
                'point_count': 4756,
                'longitude_origin': 0.0,
                'longitude_step': 0.07569386065006256
            },
            {
                'latitude': -6.7838311195373535,
                'point_count': 4752,
                'longitude_origin': 0.0,
                'longitude_step': 0.07575757801532745
            },
            {
                'latitude': -6.854129791259766,
                'point_count': 4748,
                'longitude_origin': 0.0,
                'longitude_step': 0.0758213996887207
            },
            {
                'latitude': -6.924428462982178,
                'point_count': 4744,
                'longitude_origin': 0.0,
                'longitude_step': 0.07588532567024231
            },
            {
                'latitude': -6.99472713470459,
                'point_count': 4740,
                'longitude_origin': 0.0,
                'longitude_step': 0.07594936341047287
            },
            {
                'latitude': -7.06502628326416,
                'point_count': 4736,
                'longitude_origin': 0.0,
                'longitude_step': 0.07601351290941238
            },
            {
                'latitude': -7.135324954986572,
                'point_count': 4732,
                'longitude_origin': 0.0,
                'longitude_step': 0.07607776671648026
            },
            {
                'latitude': -7.205623626708984,
                'point_count': 4728,
                'longitude_origin': 0.0,
                'longitude_step': 0.07614213228225708
            },
            {
                'latitude': -7.2759222984313965,
                'point_count': 4724,
                'longitude_origin': 0.0,
                'longitude_step': 0.07620660215616226
            },
            {
                'latitude': -7.346220970153809,
                'point_count': 4720,
                'longitude_origin': 0.0,
                'longitude_step': 0.0762711837887764
            },
            {
                'latitude': -7.416520118713379,
                'point_count': 4716,
                'longitude_origin': 0.0,
                'longitude_step': 0.07633587718009949
            },
            {
                'latitude': -7.486818790435791,
                'point_count': 4712,
                'longitude_origin': 0.0,
                'longitude_step': 0.07640068233013153
            },
            {
                'latitude': -7.557117462158203,
                'point_count': 4708,
                'longitude_origin': 0.0,
                'longitude_step': 0.07646559178829193
            },
            {
                'latitude': -7.627416133880615,
                'point_count': 4704,
                'longitude_origin': 0.0,
                'longitude_step': 0.07653061300516129
            },
            {
                'latitude': -7.697714805603027,
                'point_count': 4700,
                'longitude_origin': 0.0,
                'longitude_step': 0.0765957459807396
            },
            {
                'latitude': -7.768013954162598,
                'point_count': 4696,
                'longitude_origin': 0.0,
                'longitude_step': 0.07666099071502686
            },
            {
                'latitude': -7.83831262588501,
                'point_count': 4692,
                'longitude_origin': 0.0,
                'longitude_step': 0.07672633975744247
            },
            {
                'latitude': -7.908611297607422,
                'point_count': 4688,
                'longitude_origin': 0.0,
                'longitude_step': 0.07679180800914764
            },
            {
                'latitude': -7.978910446166992,
                'point_count': 4684,
                'longitude_origin': 0.0,
                'longitude_step': 0.07685738801956177
            },
            {
                'latitude': -8.049208641052246,
                'point_count': 4680,
                'longitude_origin': 0.0,
                'longitude_step': 0.07692307978868484
            },
            {
                'latitude': -8.119507789611816,
                'point_count': 4676,
                'longitude_origin': 0.0,
                'longitude_step': 0.07698887586593628
            },
            {
                'latitude': -8.18980598449707,
                'point_count': 4672,
                'longitude_origin': 0.0,
                'longitude_step': 0.07705479115247726
            },
            {
                'latitude': -8.26010513305664,
                'point_count': 4668,
                'longitude_origin': 0.0,
                'longitude_step': 0.0771208256483078
            },
            {
                'latitude': -8.330404281616211,
                'point_count': 4664,
                'longitude_origin': 0.0,
                'longitude_step': 0.0771869644522667
            },
            {
                'latitude': -8.400702476501465,
                'point_count': 4660,
                'longitude_origin': 0.0,
                'longitude_step': 0.07725322246551514
            },
            {
                'latitude': -8.471001625061035,
                'point_count': 4656,
                'longitude_origin': 0.0,
                'longitude_step': 0.07731958478689194
            },
            {
                'latitude': -8.541299819946289,
                'point_count': 4652,
                'longitude_origin': 0.0,
                'longitude_step': 0.07738607376813889
            },
            {
                'latitude': -8.61159896850586,
                'point_count': 4648,
                'longitude_origin': 0.0,
                'longitude_step': 0.07745266705751419
            },
            {
                'latitude': -8.68189811706543,
                'point_count': 4644,
                'longitude_origin': 0.0,
                'longitude_step': 0.07751937955617905
            },
            {
                'latitude': -8.752196311950684,
                'point_count': 4640,
                'longitude_origin': 0.0,
                'longitude_step': 0.07758620381355286
            },
            {
                'latitude': -8.822495460510254,
                'point_count': 4636,
                'longitude_origin': 0.0,
                'longitude_step': 0.07765314728021622
            },
            {
                'latitude': -8.892793655395508,
                'point_count': 4632,
                'longitude_origin': 0.0,
                'longitude_step': 0.07772020995616913
            },
            {
                'latitude': -8.963092803955078,
                'point_count': 4628,
                'longitude_origin': 0.0,
                'longitude_step': 0.077787384390831
            },
            {
                'latitude': -9.033391952514648,
                'point_count': 4624,
                'longitude_origin': 0.0,
                'longitude_step': 0.07785467058420181
            },
            {
                'latitude': -9.103690147399902,
                'point_count': 4620,
                'longitude_origin': 0.0,
                'longitude_step': 0.07792207598686218
            },
            {
                'latitude': -9.173989295959473,
                'point_count': 4616,
                'longitude_origin': 0.0,
                'longitude_step': 0.0779896005988121
            },
            {
                'latitude': -9.244287490844727,
                'point_count': 4612,
                'longitude_origin': 0.0,
                'longitude_step': 0.07805724442005157
            },
            {
                'latitude': -9.314586639404297,
                'point_count': 4608,
                'longitude_origin': 0.0,
                'longitude_step': 0.078125
            },
            {
                'latitude': -9.384885787963867,
                'point_count': 4604,
                'longitude_origin': 0.0,
                'longitude_step': 0.07819287478923798
            },
            {
                'latitude': -9.455183982849121,
                'point_count': 4600,
                'longitude_origin': 0.0,
                'longitude_step': 0.0782608687877655
            },
            {
                'latitude': -9.525483131408691,
                'point_count': 4596,
                'longitude_origin': 0.0,
                'longitude_step': 0.07832898199558258
            },
            {
                'latitude': -9.595781326293945,
                'point_count': 4592,
                'longitude_origin': 0.0,
                'longitude_step': 0.07839721441268921
            },
            {
                'latitude': -9.666080474853516,
                'point_count': 4588,
                'longitude_origin': 0.0,
                'longitude_step': 0.07846556603908539
            },
            {
                'latitude': -9.736379623413086,
                'point_count': 4584,
                'longitude_origin': 0.0,
                'longitude_step': 0.07853402942419052
            },
            {
                'latitude': -9.80667781829834,
                'point_count': 4580,
                'longitude_origin': 0.0,
                'longitude_step': 0.0786026194691658
            },
            {
                'latitude': -9.87697696685791,
                'point_count': 4576,
                'longitude_origin': 0.0,
                'longitude_step': 0.07867132872343063
            },
            {
                'latitude': -9.947275161743164,
                'point_count': 4572,
                'longitude_origin': 0.0,
                'longitude_step': 0.07874015718698502
            },
            {
                'latitude': -10.017574310302734,
                'point_count': 4568,
                'longitude_origin': 0.0,
                'longitude_step': 0.07880910485982895
            },
            {
                'latitude': -10.087873458862305,
                'point_count': 4564,
                'longitude_origin': 0.0,
                'longitude_step': 0.07887817919254303
            },
            {
                'latitude': -10.158171653747559,
                'point_count': 4560,
                'longitude_origin': 0.0,
                'longitude_step': 0.07894736528396606
            },
            {
                'latitude': -10.228470802307129,
                'point_count': 4556,
                'longitude_origin': 0.0,
                'longitude_step': 0.07901667803525925
            },
            {
                'latitude': -10.298768997192383,
                'point_count': 4552,
                'longitude_origin': 0.0,
                'longitude_step': 0.07908611744642258
            },
            {
                'latitude': -10.369068145751953,
                'point_count': 4548,
                'longitude_origin': 0.0,
                'longitude_step': 0.07915567606687546
            },
            {
                'latitude': -10.439367294311523,
                'point_count': 4544,
                'longitude_origin': 0.0,
                'longitude_step': 0.07922535389661789
            },
            {
                'latitude': -10.509665489196777,
                'point_count': 4540,
                'longitude_origin': 0.0,
                'longitude_step': 0.07929515093564987
            },
            {
                'latitude': -10.579964637756348,
                'point_count': 4536,
                'longitude_origin': 0.0,
                'longitude_step': 0.0793650820851326
            },
            {
                'latitude': -10.650262832641602,
                'point_count': 4532,
                'longitude_origin': 0.0,
                'longitude_step': 0.07943512499332428
            },
            {
                'latitude': -10.720561981201172,
                'point_count': 4528,
                'longitude_origin': 0.0,
                'longitude_step': 0.0795053020119667
            },
            {
                'latitude': -10.790861129760742,
                'point_count': 4524,
                'longitude_origin': 0.0,
                'longitude_step': 0.07957559823989868
            },
            {
                'latitude': -10.861159324645996,
                'point_count': 4520,
                'longitude_origin': 0.0,
                'longitude_step': 0.0796460211277008
            },
            {
                'latitude': -10.931458473205566,
                'point_count': 4516,
                'longitude_origin': 0.0,
                'longitude_step': 0.07971656322479248
            },
            {
                'latitude': -11.00175666809082,
                'point_count': 4512,
                'longitude_origin': 0.0,
                'longitude_step': 0.0797872319817543
            },
            {
                'latitude': -11.07205581665039,
                'point_count': 4508,
                'longitude_origin': 0.0,
                'longitude_step': 0.07985802739858627
            },
            {
                'latitude': -11.142354965209961,
                'point_count': 4504,
                'longitude_origin': 0.0,
                'longitude_step': 0.07992894947528839
            },
            {
                'latitude': -11.212653160095215,
                'point_count': 4500,
                'longitude_origin': 0.0,
                'longitude_step': 0.07999999821186066
            },
            {
                'latitude': -11.282952308654785,
                'point_count': 4496,
                'longitude_origin': 0.0,
                'longitude_step': 0.08007117360830307
            },
            {
                'latitude': -11.353250503540039,
                'point_count': 4492,
                'longitude_origin': 0.0,
                'longitude_step': 0.08014247566461563
            },
            {
                'latitude': -11.42354965209961,
                'point_count': 4488,
                'longitude_origin': 0.0,
                'longitude_step': 0.08021390438079834
            },
            {
                'latitude': -11.49384880065918,
                'point_count': 4484,
                'longitude_origin': 0.0,
                'longitude_step': 0.0802854597568512
            },
            {
                'latitude': -11.564146995544434,
                'point_count': 4480,
                'longitude_origin': 0.0,
                'longitude_step': 0.0803571417927742
            },
            {
                'latitude': -11.634446144104004,
                'point_count': 4476,
                'longitude_origin': 0.0,
                'longitude_step': 0.08042895793914795
            },
            {
                'latitude': -11.704744338989258,
                'point_count': 4472,
                'longitude_origin': 0.0,
                'longitude_step': 0.08050089329481125
            },
            {
                'latitude': -11.775043487548828,
                'point_count': 4468,
                'longitude_origin': 0.0,
                'longitude_step': 0.08057296276092529
            },
            {
                'latitude': -11.845342636108398,
                'point_count': 4464,
                'longitude_origin': 0.0,
                'longitude_step': 0.08064515888690948
            },
            {
                'latitude': -11.915640830993652,
                'point_count': 4460,
                'longitude_origin': 0.0,
                'longitude_step': 0.08071748912334442
            },
            {
                'latitude': -11.985939979553223,
                'point_count': 4456,
                'longitude_origin': 0.0,
                'longitude_step': 0.0807899460196495
            },
            {
                'latitude': -12.056238174438477,
                'point_count': 4452,
                'longitude_origin': 0.0,
                'longitude_step': 0.08086253702640533
            },
            {
                'latitude': -12.126537322998047,
                'point_count': 4448,
                'longitude_origin': 0.0,
                'longitude_step': 0.08093525469303131
            },
            {
                'latitude': -12.196836471557617,
                'point_count': 4444,
                'longitude_origin': 0.0,
                'longitude_step': 0.08100809901952744
            },
            {
                'latitude': -12.267134666442871,
                'point_count': 4440,
                'longitude_origin': 0.0,
                'longitude_step': 0.0810810774564743
            },
            {
                'latitude': -12.337433815002441,
                'point_count': 4436,
                'longitude_origin': 0.0,
                'longitude_step': 0.08115419000387192
            },
            {
                'latitude': -12.407732009887695,
                'point_count': 4432,
                'longitude_origin': 0.0,
                'longitude_step': 0.08122743666172028
            },
            {
                'latitude': -12.478031158447266,
                'point_count': 4428,
                'longitude_origin': 0.0,
                'longitude_step': 0.08130080997943878
            },
            {
                'latitude': -12.548330307006836,
                'point_count': 4424,
                'longitude_origin': 0.0,
                'longitude_step': 0.08137432485818863
            },
            {
                'latitude': -12.61862850189209,
                'point_count': 4420,
                'longitude_origin': 0.0,
                'longitude_step': 0.08144796639680862
            },
            {
                'latitude': -12.68892765045166,
                'point_count': 4416,
                'longitude_origin': 0.0,
                'longitude_step': 0.08152174204587936
            },
            {
                'latitude': -12.759225845336914,
                'point_count': 4412,
                'longitude_origin': 0.0,
                'longitude_step': 0.08159565180540085
            },
            {
                'latitude': -12.829524993896484,
                'point_count': 4408,
                'longitude_origin': 0.0,
                'longitude_step': 0.08166968822479248
            },
            {
                'latitude': -12.899824142456055,
                'point_count': 4404,
                'longitude_origin': 0.0,
                'longitude_step': 0.08174386620521545
            },
            {
                'latitude': -12.970122337341309,
                'point_count': 4400,
                'longitude_origin': 0.0,
                'longitude_step': 0.08181817829608917
            },
            {
                'latitude': -13.040421485900879,
                'point_count': 4396,
                'longitude_origin': 0.0,
                'longitude_step': 0.08189263194799423
            },
            {
                'latitude': -13.110719680786133,
                'point_count': 4392,
                'longitude_origin': 0.0,
                'longitude_step': 0.08196721225976944
            },
            {
                'latitude': -13.181018829345703,
                'point_count': 4388,
                'longitude_origin': 0.0,
                'longitude_step': 0.08204193413257599
            },
            {
                'latitude': -13.251317977905273,
                'point_count': 4384,
                'longitude_origin': 0.0,
                'longitude_step': 0.08211679011583328
            },
            {
                'latitude': -13.321616172790527,
                'point_count': 4380,
                'longitude_origin': 0.0,
                'longitude_step': 0.08219178020954132
            },
            {
                'latitude': -13.391915321350098,
                'point_count': 4376,
                'longitude_origin': 0.0,
                'longitude_step': 0.0822669118642807
            },
            {
                'latitude': -13.462214469909668,
                'point_count': 4372,
                'longitude_origin': 0.0,
                'longitude_step': 0.08234217762947083
            },
            {
                'latitude': -13.532512664794922,
                'point_count': 4368,
                'longitude_origin': 0.0,
                'longitude_step': 0.08241758495569229
            },
            {
                'latitude': -13.602811813354492,
                'point_count': 4364,
                'longitude_origin': 0.0,
                'longitude_step': 0.0824931263923645
            },
            {
                'latitude': -13.673110008239746,
                'point_count': 4360,
                'longitude_origin': 0.0,
                'longitude_step': 0.08256880939006805
            },
            {
                'latitude': -13.743409156799316,
                'point_count': 4356,
                'longitude_origin': 0.0,
                'longitude_step': 0.08264462649822235
            },
            {
                'latitude': -13.813708305358887,
                'point_count': 4352,
                'longitude_origin': 0.0,
                'longitude_step': 0.08272058516740799
            },
            {
                'latitude': -13.88400650024414,
                'point_count': 4348,
                'longitude_origin': 0.0,
                'longitude_step': 0.08279668539762497
            },
            {
                'latitude': -13.954305648803711,
                'point_count': 4344,
                'longitude_origin': 0.0,
                'longitude_step': 0.08287292718887329
            },
            {
                'latitude': -14.024603843688965,
                'point_count': 4340,
                'longitude_origin': 0.0,
                'longitude_step': 0.08294931054115295
            },
            {
                'latitude': -14.094902992248535,
                'point_count': 4336,
                'longitude_origin': 0.0,
                'longitude_step': 0.08302582800388336
            },
            {
                'latitude': -14.165202140808105,
                'point_count': 4332,
                'longitude_origin': 0.0,
                'longitude_step': 0.08310249447822571
            },
            {
                'latitude': -14.23550033569336,
                'point_count': 4328,
                'longitude_origin': 0.0,
                'longitude_step': 0.0831792950630188
            },
            {
                'latitude': -14.30579948425293,
                'point_count': 4324,
                'longitude_origin': 0.0,
                'longitude_step': 0.08325624465942383
            },
            {
                'latitude': -14.376097679138184,
                'point_count': 4320,
                'longitude_origin': 0.0,
                'longitude_step': 0.0833333358168602
            },
            {
                'latitude': -14.446396827697754,
                'point_count': 4316,
                'longitude_origin': 0.0,
                'longitude_step': 0.08341056853532791
            },
            {
                'latitude': -14.516695976257324,
                'point_count': 4312,
                'longitude_origin': 0.0,
                'longitude_step': 0.08348794281482697
            },
            {
                'latitude': -14.586994171142578,
                'point_count': 4308,
                'longitude_origin': 0.0,
                'longitude_step': 0.08356545865535736
            },
            {
                'latitude': -14.657293319702148,
                'point_count': 4304,
                'longitude_origin': 0.0,
                'longitude_step': 0.0836431235074997
            },
            {
                'latitude': -14.727591514587402,
                'point_count': 4300,
                'longitude_origin': 0.0,
                'longitude_step': 0.08372092992067337
            },
            {
                'latitude': -14.797890663146973,
                'point_count': 4296,
                'longitude_origin': 0.0,
                'longitude_step': 0.08379888534545898
            },
            {
                'latitude': -14.868189811706543,
                'point_count': 4292,
                'longitude_origin': 0.0,
                'longitude_step': 0.08387698233127594
            },
            {
                'latitude': -14.938488006591797,
                'point_count': 4288,
                'longitude_origin': 0.0,
                'longitude_step': 0.08395522087812424
            },
            {
                'latitude': -15.008787155151367,
                'point_count': 4284,
                'longitude_origin': 0.0,
                'longitude_step': 0.08403361588716507
            },
            {
                'latitude': -15.079085350036621,
                'point_count': 4280,
                'longitude_origin': 0.0,
                'longitude_step': 0.08411215245723724
            },
            {
                'latitude': -15.149384498596191,
                'point_count': 4276,
                'longitude_origin': 0.0,
                'longitude_step': 0.08419083058834076
            },
            {
                'latitude': -15.219683647155762,
                'point_count': 4272,
                'longitude_origin': 0.0,
                'longitude_step': 0.08426966518163681
            },
            {
                'latitude': -15.289981842041016,
                'point_count': 4268,
                'longitude_origin': 0.0,
                'longitude_step': 0.0843486413359642
            },
            {
                'latitude': -15.360280990600586,
                'point_count': 4264,
                'longitude_origin': 0.0,
                'longitude_step': 0.08442776650190353
            },
            {
                'latitude': -15.43057918548584,
                'point_count': 4260,
                'longitude_origin': 0.0,
                'longitude_step': 0.0845070406794548
            },
            {
                'latitude': -15.50087833404541,
                'point_count': 4256,
                'longitude_origin': 0.0,
                'longitude_step': 0.08458646386861801
            },
            {
                'latitude': -15.57117748260498,
                'point_count': 4252,
                'longitude_origin': 0.0,
                'longitude_step': 0.08466603606939316
            },
            {
                'latitude': -15.641475677490234,
                'point_count': 4248,
                'longitude_origin': 0.0,
                'longitude_step': 0.08474576473236084
            },
            {
                'latitude': -15.711774826049805,
                'point_count': 4244,
                'longitude_origin': 0.0,
                'longitude_step': 0.08482563495635986
            },
            {
                'latitude': -15.782073020935059,
                'point_count': 4240,
                'longitude_origin': 0.0,
                'longitude_step': 0.08490566164255142
            },
            {
                'latitude': -15.852372169494629,
                'point_count': 4236,
                'longitude_origin': 0.0,
                'longitude_step': 0.08498583734035492
            },
            {
                'latitude': -15.9226713180542,
                'point_count': 4232,
                'longitude_origin': 0.0,
                'longitude_step': 0.08506616204977036
            },
            {
                'latitude': -15.99297046661377,
                'point_count': 4228,
                'longitude_origin': 0.0,
                'longitude_step': 0.08514664322137833
            },
            {
                'latitude': -16.063268661499023,
                'point_count': 4224,
                'longitude_origin': 0.0,
                'longitude_step': 0.08522727340459824
            },
            {
                'latitude': -16.133567810058594,
                'point_count': 4220,
                'longitude_origin': 0.0,
                'longitude_step': 0.08530806005001068
            },
            {
                'latitude': -16.203866958618164,
                'point_count': 4216,
                'longitude_origin': 0.0,
                'longitude_step': 0.08538899570703506
            },
            {
                'latitude': -16.274166107177734,
                'point_count': 4212,
                'longitude_origin': 0.0,
                'longitude_step': 0.08547008782625198
            },
            {
                'latitude': -16.344465255737305,
                'point_count': 4208,
                'longitude_origin': 0.0,
                'longitude_step': 0.08555132895708084
            },
            {
                'latitude': -16.414762496948242,
                'point_count': 4204,
                'longitude_origin': 0.0,
                'longitude_step': 0.08563273400068283
            },
            {
                'latitude': -16.485061645507812,
                'point_count': 4200,
                'longitude_origin': 0.0,
                'longitude_step': 0.08571428805589676
            },
            {
                'latitude': -16.555360794067383,
                'point_count': 4196,
                'longitude_origin': 0.0,
                'longitude_step': 0.08579599857330322
            },
            {
                'latitude': -16.625659942626953,
                'point_count': 4192,
                'longitude_origin': 0.0,
                'longitude_step': 0.08587786555290222
            },
            {
                'latitude': -16.695959091186523,
                'point_count': 4188,
                'longitude_origin': 0.0,
                'longitude_step': 0.08595988899469376
            },
            {
                'latitude': -16.76625633239746,
                'point_count': 4184,
                'longitude_origin': 0.0,
                'longitude_step': 0.08604206144809723
            },
            {
                'latitude': -16.83655548095703,
                'point_count': 4180,
                'longitude_origin': 0.0,
                'longitude_step': 0.08612440526485443
            },
            {
                'latitude': -16.9068546295166,
                'point_count': 4176,
                'longitude_origin': 0.0,
                'longitude_step': 0.08620689809322357
            },
            {
                'latitude': -16.977153778076172,
                'point_count': 4172,
                'longitude_origin': 0.0,
                'longitude_step': 0.08628954738378525
            },
            {
                'latitude': -17.047452926635742,
                'point_count': 4168,
                'longitude_origin': 0.0,
                'longitude_step': 0.08637236058712006
            },
            {
                'latitude': -17.11775016784668,
                'point_count': 4164,
                'longitude_origin': 0.0,
                'longitude_step': 0.0864553302526474
            },
            {
                'latitude': -17.18804931640625,
                'point_count': 4160,
                'longitude_origin': 0.0,
                'longitude_step': 0.08653846383094788
            },
            {
                'latitude': -17.25834846496582,
                'point_count': 4156,
                'longitude_origin': 0.0,
                'longitude_step': 0.08662175387144089
            },
            {
                'latitude': -17.32864761352539,
                'point_count': 4152,
                'longitude_origin': 0.0,
                'longitude_step': 0.08670520037412643
            },
            {
                'latitude': -17.39894676208496,
                'point_count': 4148,
                'longitude_origin': 0.0,
                'longitude_step': 0.08678881078958511
            },
            {
                'latitude': -17.4692440032959,
                'point_count': 4144,
                'longitude_origin': 0.0,
                'longitude_step': 0.08687258511781693
            },
            {
                'latitude': -17.53954315185547,
                'point_count': 4140,
                'longitude_origin': 0.0,
                'longitude_step': 0.08695652335882187
            },
            {
                'latitude': -17.60984230041504,
                'point_count': 4136,
                'longitude_origin': 0.0,
                'longitude_step': 0.08704061806201935
            },
            {
                'latitude': -17.68014144897461,
                'point_count': 4132,
                'longitude_origin': 0.0,
                'longitude_step': 0.08712487667798996
            },
            {
                'latitude': -17.75044059753418,
                'point_count': 4128,
                'longitude_origin': 0.0,
                'longitude_step': 0.0872092992067337
            },
            {
                'latitude': -17.820737838745117,
                'point_count': 4124,
                'longitude_origin': 0.0,
                'longitude_step': 0.08729389309883118
            },
            {
                'latitude': -17.891036987304688,
                'point_count': 4120,
                'longitude_origin': 0.0,
                'longitude_step': 0.08737864345312119
            },
            {
                'latitude': -17.961336135864258,
                'point_count': 4116,
                'longitude_origin': 0.0,
                'longitude_step': 0.08746355772018433
            },
            {
                'latitude': -18.031635284423828,
                'point_count': 4112,
                'longitude_origin': 0.0,
                'longitude_step': 0.0875486359000206
            },
            {
                'latitude': -18.1019344329834,
                'point_count': 4108,
                'longitude_origin': 0.0,
                'longitude_step': 0.0876338854432106
            },
            {
                'latitude': -18.172231674194336,
                'point_count': 4104,
                'longitude_origin': 0.0,
                'longitude_step': 0.08771929889917374
            },
            {
                'latitude': -18.242530822753906,
                'point_count': 4100,
                'longitude_origin': 0.0,
                'longitude_step': 0.08780487626791
            },
            {
                'latitude': -18.312829971313477,
                'point_count': 4096,
                'longitude_origin': 0.0,
                'longitude_step': 0.087890625
            },
            {
                'latitude': -18.383129119873047,
                'point_count': 4092,
                'longitude_origin': 0.0,
                'longitude_step': 0.08797653764486313
            },
            {
                'latitude': -18.453428268432617,
                'point_count': 4088,
                'longitude_origin': 0.0,
                'longitude_step': 0.08806262165307999
            },
            {
                'latitude': -18.523725509643555,
                'point_count': 4084,
                'longitude_origin': 0.0,
                'longitude_step': 0.08814887702465057
            },
            {
                'latitude': -18.594024658203125,
                'point_count': 4080,
                'longitude_origin': 0.0,
                'longitude_step': 0.0882352963089943
            },
            {
                'latitude': -18.664323806762695,
                'point_count': 4076,
                'longitude_origin': 0.0,
                'longitude_step': 0.08832188695669174
            },
            {
                'latitude': -18.734622955322266,
                'point_count': 4072,
                'longitude_origin': 0.0,
                'longitude_step': 0.08840864151716232
            },
            {
                'latitude': -18.804922103881836,
                'point_count': 4068,
                'longitude_origin': 0.0,
                'longitude_step': 0.08849557489156723
            },
            {
                'latitude': -18.875219345092773,
                'point_count': 4064,
                'longitude_origin': 0.0,
                'longitude_step': 0.08858267962932587
            },
            {
                'latitude': -18.945518493652344,
                'point_count': 4060,
                'longitude_origin': 0.0,
                'longitude_step': 0.08866994827985764
            },
            {
                'latitude': -19.015817642211914,
                'point_count': 4056,
                'longitude_origin': 0.0,
                'longitude_step': 0.08875739574432373
            },
            {
                'latitude': -19.086116790771484,
                'point_count': 4052,
                'longitude_origin': 0.0,
                'longitude_step': 0.08884501457214355
            },
            {
                'latitude': -19.156415939331055,
                'point_count': 4048,
                'longitude_origin': 0.0,
                'longitude_step': 0.08893280476331711
            },
            {
                'latitude': -19.226713180541992,
                'point_count': 4044,
                'longitude_origin': 0.0,
                'longitude_step': 0.08902077376842499
            },
            {
                'latitude': -19.297012329101562,
                'point_count': 4040,
                'longitude_origin': 0.0,
                'longitude_step': 0.0891089141368866
            },
            {
                'latitude': -19.367311477661133,
                'point_count': 4036,
                'longitude_origin': 0.0,
                'longitude_step': 0.08919722586870193
            },
            {
                'latitude': -19.437610626220703,
                'point_count': 4032,
                'longitude_origin': 0.0,
                'longitude_step': 0.0892857164144516
            },
            {
                'latitude': -19.507909774780273,
                'point_count': 4028,
                'longitude_origin': 0.0,
                'longitude_step': 0.08937437832355499
            },
            {
                'latitude': -19.57820701599121,
                'point_count': 4024,
                'longitude_origin': 0.0,
                'longitude_step': 0.08946321904659271
            },
            {
                'latitude': -19.64850616455078,
                'point_count': 4020,
                'longitude_origin': 0.0,
                'longitude_step': 0.08955223858356476
            },
            {
                'latitude': -19.71880531311035,
                'point_count': 4016,
                'longitude_origin': 0.0,
                'longitude_step': 0.08964143693447113
            },
            {
                'latitude': -19.789104461669922,
                'point_count': 4012,
                'longitude_origin': 0.0,
                'longitude_step': 0.08973080664873123
            },
            {
                'latitude': -19.859403610229492,
                'point_count': 4008,
                'longitude_origin': 0.0,
                'longitude_step': 0.08982036262750626
            },
            {
                'latitude': -19.92970085144043,
                'point_count': 4004,
                'longitude_origin': 0.0,
                'longitude_step': 0.08991008996963501
            },
            {
                'latitude': -20.0,
                'point_count': 4000,
                'longitude_origin': 0.0,
                'longitude_step': 0.09000000357627869
            },
            {
                'latitude': -20.07029914855957,
                'point_count': 3996,
                'longitude_origin': 0.0,
                'longitude_step': 0.09009008854627609
            },
            {
                'latitude': -20.14059829711914,
                'point_count': 3992,
                'longitude_origin': 0.0,
                'longitude_step': 0.09018035978078842
            },
            {
                'latitude': -20.21089744567871,
                'point_count': 3988,
                'longitude_origin': 0.0,
                'longitude_step': 0.09027080982923508
            },
            {
                'latitude': -20.28119468688965,
                'point_count': 3984,
                'longitude_origin': 0.0,
                'longitude_step': 0.09036144614219666
            },
            {
                'latitude': -20.35149383544922,
                'point_count': 3980,
                'longitude_origin': 0.0,
                'longitude_step': 0.09045226126909256
            },
            {
                'latitude': -20.42179298400879,
                'point_count': 3976,
                'longitude_origin': 0.0,
                'longitude_step': 0.09054326266050339
            },
            {
                'latitude': -20.49209213256836,
                'point_count': 3972,
                'longitude_origin': 0.0,
                'longitude_step': 0.09063444286584854
            },
            {
                'latitude': -20.56239128112793,
                'point_count': 3968,
                'longitude_origin': 0.0,
                'longitude_step': 0.09072580933570862
            },
            {
                'latitude': -20.632688522338867,
                'point_count': 3964,
                'longitude_origin': 0.0,
                'longitude_step': 0.09081735461950302
            },
            {
                'latitude': -20.702987670898438,
                'point_count': 3960,
                'longitude_origin': 0.0,
                'longitude_step': 0.09090909361839294
            },
            {
                'latitude': -20.773286819458008,
                'point_count': 3956,
                'longitude_origin': 0.0,
                'longitude_step': 0.0910010114312172
            },
            {
                'latitude': -20.843585968017578,
                'point_count': 3952,
                'longitude_origin': 0.0,
                'longitude_step': 0.09109311550855637
            },
            {
                'latitude': -20.91388511657715,
                'point_count': 3948,
                'longitude_origin': 0.0,
                'longitude_step': 0.09118541330099106
            },
            {
                'latitude': -20.984182357788086,
                'point_count': 3944,
                'longitude_origin': 0.0,
                'longitude_step': 0.09127788990736008
            },
            {
                'latitude': -21.054481506347656,
                'point_count': 3940,
                'longitude_origin': 0.0,
                'longitude_step': 0.09137056022882462
            },
            {
                'latitude': -21.124780654907227,
                'point_count': 3936,
                'longitude_origin': 0.0,
                'longitude_step': 0.09146341681480408
            },
            {
                'latitude': -21.195079803466797,
                'point_count': 3932,
                'longitude_origin': 0.0,
                'longitude_step': 0.09155645966529846
            },
            {
                'latitude': -21.265378952026367,
                'point_count': 3928,
                'longitude_origin': 0.0,
                'longitude_step': 0.09164969623088837
            },
            {
                'latitude': -21.335676193237305,
                'point_count': 3924,
                'longitude_origin': 0.0,
                'longitude_step': 0.0917431190609932
            },
            {
                'latitude': -21.405975341796875,
                'point_count': 3920,
                'longitude_origin': 0.0,
                'longitude_step': 0.09183673560619354
            },
            {
                'latitude': -21.476274490356445,
                'point_count': 3916,
                'longitude_origin': 0.0,
                'longitude_step': 0.09193053841590881
            },
            {
                'latitude': -21.546573638916016,
                'point_count': 3912,
                'longitude_origin': 0.0,
                'longitude_step': 0.0920245423913002
            },
            {
                'latitude': -21.616872787475586,
                'point_count': 3908,
                'longitude_origin': 0.0,
                'longitude_step': 0.09211873263120651
            },
            {
                'latitude': -21.687170028686523,
                'point_count': 3904,
                'longitude_origin': 0.0,
                'longitude_step': 0.09221311658620834
            },
            {
                'latitude': -21.757469177246094,
                'point_count': 3900,
                'longitude_origin': 0.0,
                'longitude_step': 0.0923076942563057
            },
            {
                'latitude': -21.827768325805664,
                'point_count': 3896,
                'longitude_origin': 0.0,
                'longitude_step': 0.09240246564149857
            },
            {
                'latitude': -21.898067474365234,
                'point_count': 3892,
                'longitude_origin': 0.0,
                'longitude_step': 0.09249743074178696
            },
            {
                'latitude': -21.968366622924805,
                'point_count': 3888,
                'longitude_origin': 0.0,
                'longitude_step': 0.09259258955717087
            },
            {
                'latitude': -22.038663864135742,
                'point_count': 3884,
                'longitude_origin': 0.0,
                'longitude_step': 0.0926879495382309
            },
            {
                'latitude': -22.108963012695312,
                'point_count': 3880,
                'longitude_origin': 0.0,
                'longitude_step': 0.09278350323438644
            },
            {
                'latitude': -22.179262161254883,
                'point_count': 3876,
                'longitude_origin': 0.0,
                'longitude_step': 0.09287925809621811
            },
            {
                'latitude': -22.249561309814453,
                'point_count': 3872,
                'longitude_origin': 0.0,
                'longitude_step': 0.0929752066731453
            },
            {
                'latitude': -22.319860458374023,
                'point_count': 3868,
                'longitude_origin': 0.0,
                'longitude_step': 0.0930713564157486
            },
            {
                'latitude': -22.39015769958496,
                'point_count': 3864,
                'longitude_origin': 0.0,
                'longitude_step': 0.09316769987344742
            },
            {
                'latitude': -22.46045684814453,
                'point_count': 3860,
                'longitude_origin': 0.0,
                'longitude_step': 0.09326425194740295
            },
            {
                'latitude': -22.5307559967041,
                'point_count': 3856,
                'longitude_origin': 0.0,
                'longitude_step': 0.09336099773645401
            },
            {
                'latitude': -22.601055145263672,
                'point_count': 3852,
                'longitude_origin': 0.0,
                'longitude_step': 0.09345794469118118
            },
            {
                'latitude': -22.671354293823242,
                'point_count': 3848,
                'longitude_origin': 0.0,
                'longitude_step': 0.09355509281158447
            },
            {
                'latitude': -22.74165153503418,
                'point_count': 3844,
                'longitude_origin': 0.0,
                'longitude_step': 0.09365244209766388
            },
            {
                'latitude': -22.81195068359375,
                'point_count': 3840,
                'longitude_origin': 0.0,
                'longitude_step': 0.09375
            },
            {
                'latitude': -22.88224983215332,
                'point_count': 3836,
                'longitude_origin': 0.0,
                'longitude_step': 0.09384775906801224
            },
            {
                'latitude': -22.95254898071289,
                'point_count': 3832,
                'longitude_origin': 0.0,
                'longitude_step': 0.09394571930170059
            },
            {
                'latitude': -23.02284812927246,
                'point_count': 3828,
                'longitude_origin': 0.0,
                'longitude_step': 0.09404388815164566
            },
            {
                'latitude': -23.0931453704834,
                'point_count': 3824,
                'longitude_origin': 0.0,
                'longitude_step': 0.09414225816726685
            },
            {
                'latitude': -23.16344451904297,
                'point_count': 3820,
                'longitude_origin': 0.0,
                'longitude_step': 0.09424083679914474
            },
            {
                'latitude': -23.23374366760254,
                'point_count': 3816,
                'longitude_origin': 0.0,
                'longitude_step': 0.09433962404727936
            },
            {
                'latitude': -23.30404281616211,
                'point_count': 3812,
                'longitude_origin': 0.0,
                'longitude_step': 0.09443861246109009
            },
            {
                'latitude': -23.37434196472168,
                'point_count': 3808,
                'longitude_origin': 0.0,
                'longitude_step': 0.09453781694173813
            },
            {
                'latitude': -23.444639205932617,
                'point_count': 3804,
                'longitude_origin': 0.0,
                'longitude_step': 0.09463722258806229
            },
            {
                'latitude': -23.514938354492188,
                'point_count': 3800,
                'longitude_origin': 0.0,
                'longitude_step': 0.09473684430122375
            },
            {
                'latitude': -23.585237503051758,
                'point_count': 3796,
                'longitude_origin': 0.0,
                'longitude_step': 0.09483666718006134
            },
            {
                'latitude': -23.655536651611328,
                'point_count': 3792,
                'longitude_origin': 0.0,
                'longitude_step': 0.09493670612573624
            },
            {
                'latitude': -23.7258358001709,
                'point_count': 3788,
                'longitude_origin': 0.0,
                'longitude_step': 0.09503696113824844
            },
            {
                'latitude': -23.796133041381836,
                'point_count': 3784,
                'longitude_origin': 0.0,
                'longitude_step': 0.09513741731643677
            },
            {
                'latitude': -23.866432189941406,
                'point_count': 3780,
                'longitude_origin': 0.0,
                'longitude_step': 0.095238097012043
            },
            {
                'latitude': -23.936731338500977,
                'point_count': 3776,
                'longitude_origin': 0.0,
                'longitude_step': 0.09533898532390594
            },
            {
                'latitude': -24.007030487060547,
                'point_count': 3772,
                'longitude_origin': 0.0,
                'longitude_step': 0.0954400822520256
            },
            {
                'latitude': -24.077329635620117,
                'point_count': 3768,
                'longitude_origin': 0.0,
                'longitude_step': 0.09554140269756317
            },
            {
                'latitude': -24.147626876831055,
                'point_count': 3764,
                'longitude_origin': 0.0,
                'longitude_step': 0.09564293175935745
            },
            {
                'latitude': -24.217926025390625,
                'point_count': 3760,
                'longitude_origin': 0.0,
                'longitude_step': 0.09574468433856964
            },
            {
                'latitude': -24.288225173950195,
                'point_count': 3756,
                'longitude_origin': 0.0,
                'longitude_step': 0.09584664553403854
            },
            {
                'latitude': -24.358524322509766,
                'point_count': 3752,
                'longitude_origin': 0.0,
                'longitude_step': 0.09594883024692535
            },
            {
                'latitude': -24.428823471069336,
                'point_count': 3748,
                'longitude_origin': 0.0,
                'longitude_step': 0.09605123102664948
            },
            {
                'latitude': -24.499120712280273,
                'point_count': 3744,
                'longitude_origin': 0.0,
                'longitude_step': 0.09615384787321091
            },
            {
                'latitude': -24.569419860839844,
                'point_count': 3740,
                'longitude_origin': 0.0,
                'longitude_step': 0.09625668078660965
            },
            {
                'latitude': -24.639719009399414,
                'point_count': 3736,
                'longitude_origin': 0.0,
                'longitude_step': 0.0963597446680069
            },
            {
                'latitude': -24.710018157958984,
                'point_count': 3732,
                'longitude_origin': 0.0,
                'longitude_step': 0.09646302461624146
            },
            {
                'latitude': -24.780317306518555,
                'point_count': 3728,
                'longitude_origin': 0.0,
                'longitude_step': 0.09656652063131332
            },
            {
                'latitude': -24.850614547729492,
                'point_count': 3724,
                'longitude_origin': 0.0,
                'longitude_step': 0.0966702476143837
            },
            {
                'latitude': -24.920913696289062,
                'point_count': 3720,
                'longitude_origin': 0.0,
                'longitude_step': 0.09677419066429138
            },
            {
                'latitude': -24.991212844848633,
                'point_count': 3716,
                'longitude_origin': 0.0,
                'longitude_step': 0.09687836468219757
            },
            {
                'latitude': -25.061511993408203,
                'point_count': 3712,
                'longitude_origin': 0.0,
                'longitude_step': 0.09698276221752167
            },
            {
                'latitude': -25.131811141967773,
                'point_count': 3708,
                'longitude_origin': 0.0,
                'longitude_step': 0.09708737581968307
            },
            {
                'latitude': -25.20210838317871,
                'point_count': 3704,
                'longitude_origin': 0.0,
                'longitude_step': 0.09719222784042358
            },
            {
                'latitude': -25.27240753173828,
                'point_count': 3700,
                'longitude_origin': 0.0,
                'longitude_step': 0.0972972959280014
            },
            {
                'latitude': -25.34270668029785,
                'point_count': 3696,
                'longitude_origin': 0.0,
                'longitude_step': 0.09740259498357773
            },
            {
                'latitude': -25.413005828857422,
                'point_count': 3692,
                'longitude_origin': 0.0,
                'longitude_step': 0.09750812500715256
            },
            {
                'latitude': -25.483304977416992,
                'point_count': 3688,
                'longitude_origin': 0.0,
                'longitude_step': 0.09761388599872589
            },
            {
                'latitude': -25.55360221862793,
                'point_count': 3684,
                'longitude_origin': 0.0,
                'longitude_step': 0.09771987050771713
            },
            {
                'latitude': -25.6239013671875,
                'point_count': 3680,
                'longitude_origin': 0.0,
                'longitude_step': 0.09782608598470688
            },
            {
                'latitude': -25.69420051574707,
                'point_count': 3676,
                'longitude_origin': 0.0,
                'longitude_step': 0.09793253242969513
            },
            {
                'latitude': -25.76449966430664,
                'point_count': 3672,
                'longitude_origin': 0.0,
                'longitude_step': 0.09803921729326248
            },
            {
                'latitude': -25.83479881286621,
                'point_count': 3668,
                'longitude_origin': 0.0,
                'longitude_step': 0.09814612567424774
            },
            {
                'latitude': -25.90509605407715,
                'point_count': 3664,
                'longitude_origin': 0.0,
                'longitude_step': 0.0982532724738121
            },
            {
                'latitude': -25.97539520263672,
                'point_count': 3660,
                'longitude_origin': 0.0,
                'longitude_step': 0.09836065769195557
            },
            {
                'latitude': -26.04569435119629,
                'point_count': 3656,
                'longitude_origin': 0.0,
                'longitude_step': 0.09846827387809753
            },
            {
                'latitude': -26.11599349975586,
                'point_count': 3652,
                'longitude_origin': 0.0,
                'longitude_step': 0.098576121032238
            },
            {
                'latitude': -26.18629264831543,
                'point_count': 3648,
                'longitude_origin': 0.0,
                'longitude_step': 0.09868421405553818
            },
            {
                'latitude': -26.256589889526367,
                'point_count': 3644,
                'longitude_origin': 0.0,
                'longitude_step': 0.09879253804683685
            },
            {
                'latitude': -26.326889038085938,
                'point_count': 3640,
                'longitude_origin': 0.0,
                'longitude_step': 0.09890110045671463
            },
            {
                'latitude': -26.397188186645508,
                'point_count': 3636,
                'longitude_origin': 0.0,
                'longitude_step': 0.09900990128517151
            },
            {
                'latitude': -26.467487335205078,
                'point_count': 3632,
                'longitude_origin': 0.0,
                'longitude_step': 0.09911894053220749
            },
            {
                'latitude': -26.53778648376465,
                'point_count': 3628,
                'longitude_origin': 0.0,
                'longitude_step': 0.09922822564840317
            },
            {
                'latitude': -26.608083724975586,
                'point_count': 3624,
                'longitude_origin': 0.0,
                'longitude_step': 0.09933774918317795
            },
            {
                'latitude': -26.678382873535156,
                'point_count': 3620,
                'longitude_origin': 0.0,
                'longitude_step': 0.09944751113653183
            },
            {
                'latitude': -26.748682022094727,
                'point_count': 3616,
                'longitude_origin': 0.0,
                'longitude_step': 0.09955751895904541
            },
            {
                'latitude': -26.818981170654297,
                'point_count': 3612,
                'longitude_origin': 0.0,
                'longitude_step': 0.09966777265071869
            },
            {
                'latitude': -26.889280319213867,
                'point_count': 3608,
                'longitude_origin': 0.0,
                'longitude_step': 0.09977827221155167
            },
            {
                'latitude': -26.959579467773438,
                'point_count': 3604,
                'longitude_origin': 0.0,
                'longitude_step': 0.09988901019096375
            },
            {
                'latitude': -27.029876708984375,
                'point_count': 3600,
                'longitude_origin': 0.0,
                'longitude_step': 0.10000000149011612
            },
            {
                'latitude': -27.100175857543945,
                'point_count': 3596,
                'longitude_origin': 0.0,
                'longitude_step': 0.1001112312078476
            },
            {
                'latitude': -27.170475006103516,
                'point_count': 3592,
                'longitude_origin': 0.0,
                'longitude_step': 0.10022271424531937
            },
            {
                'latitude': -27.240774154663086,
                'point_count': 3588,
                'longitude_origin': 0.0,
                'longitude_step': 0.10033445060253143
            },
            {
                'latitude': -27.311073303222656,
                'point_count': 3584,
                'longitude_origin': 0.0,
                'longitude_step': 0.1004464253783226
            },
            {
                'latitude': -27.381370544433594,
                'point_count': 3580,
                'longitude_origin': 0.0,
                'longitude_step': 0.10055866092443466
            },
            {
                'latitude': -27.451669692993164,
                'point_count': 3576,
                'longitude_origin': 0.0,
                'longitude_step': 0.10067114233970642
            },
            {
                'latitude': -27.521968841552734,
                'point_count': 3572,
                'longitude_origin': 0.0,
                'longitude_step': 0.10078387707471848
            },
            {
                'latitude': -27.592267990112305,
                'point_count': 3568,
                'longitude_origin': 0.0,
                'longitude_step': 0.10089685767889023
            },
            {
                'latitude': -27.662567138671875,
                'point_count': 3564,
                'longitude_origin': 0.0,
                'longitude_step': 0.10101009905338287
            },
            {
                'latitude': -27.732864379882812,
                'point_count': 3560,
                'longitude_origin': 0.0,
                'longitude_step': 0.10112359374761581
            },
            {
                'latitude': -27.803163528442383,
                'point_count': 3556,
                'longitude_origin': 0.0,
                'longitude_step': 0.10123734176158905
            },
            {
                'latitude': -27.873462677001953,
                'point_count': 3552,
                'longitude_origin': 0.0,
                'longitude_step': 0.10135135054588318
            },
            {
                'latitude': -27.943761825561523,
                'point_count': 3548,
                'longitude_origin': 0.0,
                'longitude_step': 0.1014656126499176
            },
            {
                'latitude': -28.014060974121094,
                'point_count': 3544,
                'longitude_origin': 0.0,
                'longitude_step': 0.10158013552427292
            },
            {
                'latitude': -28.08435821533203,
                'point_count': 3540,
                'longitude_origin': 0.0,
                'longitude_step': 0.10169491171836853
            },
            {
                'latitude': -28.1546573638916,
                'point_count': 3536,
                'longitude_origin': 0.0,
                'longitude_step': 0.10180995613336563
            },
            {
                'latitude': -28.224956512451172,
                'point_count': 3532,
                'longitude_origin': 0.0,
                'longitude_step': 0.10192525386810303
            },
            {
                'latitude': -28.295255661010742,
                'point_count': 3528,
                'longitude_origin': 0.0,
                'longitude_step': 0.10204081982374191
            },
            {
                'latitude': -28.365554809570312,
                'point_count': 3524,
                'longitude_origin': 0.0,
                'longitude_step': 0.1021566390991211
            },
            {
                'latitude': -28.43585205078125,
                'point_count': 3520,
                'longitude_origin': 0.0,
                'longitude_step': 0.10227272659540176
            },
            {
                'latitude': -28.50615119934082,
                'point_count': 3516,
                'longitude_origin': 0.0,
                'longitude_step': 0.10238907486200333
            },
            {
                'latitude': -28.57645034790039,
                'point_count': 3512,
                'longitude_origin': 0.0,
                'longitude_step': 0.10250569134950638
            },
            {
                'latitude': -28.64674949645996,
                'point_count': 3508,
                'longitude_origin': 0.0,
                'longitude_step': 0.10262257605791092
            },
            {
                'latitude': -28.71704864501953,
                'point_count': 3504,
                'longitude_origin': 0.0,
                'longitude_step': 0.10273972898721695
            },
            {
                'latitude': -28.78734588623047,
                'point_count': 3500,
                'longitude_origin': 0.0,
                'longitude_step': 0.10285714268684387
            },
            {
                'latitude': -28.85764503479004,
                'point_count': 3496,
                'longitude_origin': 0.0,
                'longitude_step': 0.10297483205795288
            },
            {
                'latitude': -28.92794418334961,
                'point_count': 3492,
                'longitude_origin': 0.0,
                'longitude_step': 0.10309278219938278
            },
            {
                'latitude': -28.99824333190918,
                'point_count': 3488,
                'longitude_origin': 0.0,
                'longitude_step': 0.10321100801229477
            },
            {
                'latitude': -29.06854248046875,
                'point_count': 3484,
                'longitude_origin': 0.0,
                'longitude_step': 0.10332950949668884
            },
            {
                'latitude': -29.138839721679688,
                'point_count': 3480,
                'longitude_origin': 0.0,
                'longitude_step': 0.1034482792019844
            },
            {
                'latitude': -29.209138870239258,
                'point_count': 3476,
                'longitude_origin': 0.0,
                'longitude_step': 0.10356731712818146
            },
            {
                'latitude': -29.279438018798828,
                'point_count': 3472,
                'longitude_origin': 0.0,
                'longitude_step': 0.10368663817644119
            },
            {
                'latitude': -29.3497371673584,
                'point_count': 3468,
                'longitude_origin': 0.0,
                'longitude_step': 0.10380622744560242
            },
            {
                'latitude': -29.42003631591797,
                'point_count': 3464,
                'longitude_origin': 0.0,
                'longitude_step': 0.10392609983682632
            },
            {
                'latitude': -29.490333557128906,
                'point_count': 3460,
                'longitude_origin': 0.0,
                'longitude_step': 0.10404624044895172
            },
            {
                'latitude': -29.560632705688477,
                'point_count': 3456,
                'longitude_origin': 0.0,
                'longitude_step': 0.1041666641831398
            },
            {
                'latitude': -29.630931854248047,
                'point_count': 3452,
                'longitude_origin': 0.0,
                'longitude_step': 0.10428737103939056
            },
            {
                'latitude': -29.701231002807617,
                'point_count': 3448,
                'longitude_origin': 0.0,
                'longitude_step': 0.10440835356712341
            },
            {
                'latitude': -29.771530151367188,
                'point_count': 3444,
                'longitude_origin': 0.0,
                'longitude_step': 0.10452961921691895
            },
            {
                'latitude': -29.841827392578125,
                'point_count': 3440,
                'longitude_origin': 0.0,
                'longitude_step': 0.10465116053819656
            },
            {
                'latitude': -29.912126541137695,
                'point_count': 3436,
                'longitude_origin': 0.0,
                'longitude_step': 0.10477299243211746
            },
            {
                'latitude': -29.982425689697266,
                'point_count': 3432,
                'longitude_origin': 0.0,
                'longitude_step': 0.10489510744810104
            },
            {
                'latitude': -30.052724838256836,
                'point_count': 3428,
                'longitude_origin': 0.0,
                'longitude_step': 0.10501750558614731
            },
            {
                'latitude': -30.123023986816406,
                'point_count': 3424,
                'longitude_origin': 0.0,
                'longitude_step': 0.10514018684625626
            },
            {
                'latitude': -30.193321228027344,
                'point_count': 3420,
                'longitude_origin': 0.0,
                'longitude_step': 0.10526315867900848
            },
            {
                'latitude': -30.263620376586914,
                'point_count': 3416,
                'longitude_origin': 0.0,
                'longitude_step': 0.1053864136338234
            },
            {
                'latitude': -30.333919525146484,
                'point_count': 3412,
                'longitude_origin': 0.0,
                'longitude_step': 0.10550996661186218
            },
            {
                'latitude': -30.404218673706055,
                'point_count': 3408,
                'longitude_origin': 0.0,
                'longitude_step': 0.10563380271196365
            },
            {
                'latitude': -30.474517822265625,
                'point_count': 3404,
                'longitude_origin': 0.0,
                'longitude_step': 0.1057579293847084
            },
            {
                'latitude': -30.544815063476562,
                'point_count': 3400,
                'longitude_origin': 0.0,
                'longitude_step': 0.10588235408067703
            },
            {
                'latitude': -30.615114212036133,
                'point_count': 3396,
                'longitude_origin': 0.0,
                'longitude_step': 0.10600706934928894
            },
            {
                'latitude': -30.685413360595703,
                'point_count': 3392,
                'longitude_origin': 0.0,
                'longitude_step': 0.10613207519054413
            },
            {
                'latitude': -30.755712509155273,
                'point_count': 3388,
                'longitude_origin': 0.0,
                'longitude_step': 0.1062573790550232
            },
            {
                'latitude': -30.826011657714844,
                'point_count': 3384,
                'longitude_origin': 0.0,
                'longitude_step': 0.10638298094272614
            },
            {
                'latitude': -30.89630889892578,
                'point_count': 3380,
                'longitude_origin': 0.0,
                'longitude_step': 0.10650887340307236
            },
            {
                'latitude': -30.96660804748535,
                'point_count': 3376,
                'longitude_origin': 0.0,
                'longitude_step': 0.10663507133722305
            },
            {
                'latitude': -31.036907196044922,
                'point_count': 3372,
                'longitude_origin': 0.0,
                'longitude_step': 0.10676156729459763
            },
            {
                'latitude': -31.107206344604492,
                'point_count': 3368,
                'longitude_origin': 0.0,
                'longitude_step': 0.10688836127519608
            },
            {
                'latitude': -31.177505493164062,
                'point_count': 3364,
                'longitude_origin': 0.0,
                'longitude_step': 0.107015460729599
            },
            {
                'latitude': -31.247802734375,
                'point_count': 3360,
                'longitude_origin': 0.0,
                'longitude_step': 0.1071428582072258
            },
            {
                'latitude': -31.31810188293457,
                'point_count': 3356,
                'longitude_origin': 0.0,
                'longitude_step': 0.10727056115865707
            },
            {
                'latitude': -31.38840103149414,
                'point_count': 3352,
                'longitude_origin': 0.0,
                'longitude_step': 0.10739856958389282
            },
            {
                'latitude': -31.45870018005371,
                'point_count': 3348,
                'longitude_origin': 0.0,
                'longitude_step': 0.10752688348293304
            },
            {
                'latitude': -31.52899932861328,
                'point_count': 3344,
                'longitude_origin': 0.0,
                'longitude_step': 0.10765550285577774
            },
            {
                'latitude': -31.59929656982422,
                'point_count': 3340,
                'longitude_origin': 0.0,
                'longitude_step': 0.10778442770242691
            },
            {
                'latitude': -31.66959571838379,
                'point_count': 3336,
                'longitude_origin': 0.0,
                'longitude_step': 0.10791366547346115
            },
            {
                'latitude': -31.73989486694336,
                'point_count': 3332,
                'longitude_origin': 0.0,
                'longitude_step': 0.10804321616888046
            },
            {
                'latitude': -31.81019401550293,
                'point_count': 3328,
                'longitude_origin': 0.0,
                'longitude_step': 0.10817307978868484
            },
            {
                'latitude': -31.8804931640625,
                'point_count': 3324,
                'longitude_origin': 0.0,
                'longitude_step': 0.1083032488822937
            },
            {
                'latitude': -31.950790405273438,
                'point_count': 3320,
                'longitude_origin': 0.0,
                'longitude_step': 0.10843373835086823
            },
            {
                'latitude': -32.02109146118164,
                'point_count': 3316,
                'longitude_origin': 0.0,
                'longitude_step': 0.10856453329324722
            },
            {
                'latitude': -32.09138870239258,
                'point_count': 3312,
                'longitude_origin': 0.0,
                'longitude_step': 0.10869564861059189
            },
            {
                'latitude': -32.161685943603516,
                'point_count': 3308,
                'longitude_origin': 0.0,
                'longitude_step': 0.10882708430290222
            },
            {
                'latitude': -32.23198699951172,
                'point_count': 3304,
                'longitude_origin': 0.0,
                'longitude_step': 0.10895884037017822
            },
            {
                'latitude': -32.302284240722656,
                'point_count': 3300,
                'longitude_origin': 0.0,
                'longitude_step': 0.1090909093618393
            },
            {
                'latitude': -32.37258529663086,
                'point_count': 3296,
                'longitude_origin': 0.0,
                'longitude_step': 0.10922329872846603
            },
            {
                'latitude': -32.4428825378418,
                'point_count': 3292,
                'longitude_origin': 0.0,
                'longitude_step': 0.10935601592063904
            },
            {
                'latitude': -32.513179779052734,
                'point_count': 3288,
                'longitude_origin': 0.0,
                'longitude_step': 0.10948905348777771
            },
            {
                'latitude': -32.58348083496094,
                'point_count': 3284,
                'longitude_origin': 0.0,
                'longitude_step': 0.10962241142988205
            },
            {
                'latitude': -32.653778076171875,
                'point_count': 3280,
                'longitude_origin': 0.0,
                'longitude_step': 0.10975609719753265
            },
            {
                'latitude': -32.72407913208008,
                'point_count': 3276,
                'longitude_origin': 0.0,
                'longitude_step': 0.10989011079072952
            },
            {
                'latitude': -32.794376373291016,
                'point_count': 3272,
                'longitude_origin': 0.0,
                'longitude_step': 0.11002445220947266
            },
            {
                'latitude': -32.86467361450195,
                'point_count': 3268,
                'longitude_origin': 0.0,
                'longitude_step': 0.11015912145376205
            },
            {
                'latitude': -32.934974670410156,
                'point_count': 3264,
                'longitude_origin': 0.0,
                'longitude_step': 0.11029411852359772
            },
            {
                'latitude': -33.005271911621094,
                'point_count': 3260,
                'longitude_origin': 0.0,
                'longitude_step': 0.11042945086956024
            },
            {
                'latitude': -33.0755729675293,
                'point_count': 3256,
                'longitude_origin': 0.0,
                'longitude_step': 0.11056511104106903
            },
            {
                'latitude': -33.145870208740234,
                'point_count': 3252,
                'longitude_origin': 0.0,
                'longitude_step': 0.11070110648870468
            },
            {
                'latitude': -33.21616744995117,
                'point_count': 3248,
                'longitude_origin': 0.0,
                'longitude_step': 0.1108374372124672
            },
            {
                'latitude': -33.286468505859375,
                'point_count': 3244,
                'longitude_origin': 0.0,
                'longitude_step': 0.11097410321235657
            },
            {
                'latitude': -33.35676574707031,
                'point_count': 3240,
                'longitude_origin': 0.0,
                'longitude_step': 0.1111111119389534
            },
            {
                'latitude': -33.427066802978516,
                'point_count': 3236,
                'longitude_origin': 0.0,
                'longitude_step': 0.1112484559416771
            },
            {
                'latitude': -33.49736404418945,
                'point_count': 3232,
                'longitude_origin': 0.0,
                'longitude_step': 0.11138613522052765
            },
            {
                'latitude': -33.56766128540039,
                'point_count': 3228,
                'longitude_origin': 0.0,
                'longitude_step': 0.11152416467666626
            },
            {
                'latitude': -33.637962341308594,
                'point_count': 3224,
                'longitude_origin': 0.0,
                'longitude_step': 0.11166252940893173
            },
            {
                'latitude': -33.70825958251953,
                'point_count': 3220,
                'longitude_origin': 0.0,
                'longitude_step': 0.11180124431848526
            },
            {
                'latitude': -33.778560638427734,
                'point_count': 3216,
                'longitude_origin': 0.0,
                'longitude_step': 0.11194030195474625
            },
            {
                'latitude': -33.84885787963867,
                'point_count': 3212,
                'longitude_origin': 0.0,
                'longitude_step': 0.11207970231771469
            },
            {
                'latitude': -33.91915512084961,
                'point_count': 3208,
                'longitude_origin': 0.0,
                'longitude_step': 0.11221945285797119
            },
            {
                'latitude': -33.98945617675781,
                'point_count': 3204,
                'longitude_origin': 0.0,
                'longitude_step': 0.11235955357551575
            },
            {
                'latitude': -34.05975341796875,
                'point_count': 3200,
                'longitude_origin': 0.0,
                'longitude_step': 0.11249999701976776
            },
            {
                'latitude': -34.13005447387695,
                'point_count': 3196,
                'longitude_origin': 0.0,
                'longitude_step': 0.11264079809188843
            },
            {
                'latitude': -34.20035171508789,
                'point_count': 3192,
                'longitude_origin': 0.0,
                'longitude_step': 0.11278195679187775
            },
            {
                'latitude': -34.27064895629883,
                'point_count': 3188,
                'longitude_origin': 0.0,
                'longitude_step': 0.11292346566915512
            },
            {
                'latitude': -34.34095001220703,
                'point_count': 3184,
                'longitude_origin': 0.0,
                'longitude_step': 0.11306532472372055
            },
            {
                'latitude': -34.41124725341797,
                'point_count': 3180,
                'longitude_origin': 0.0,
                'longitude_step': 0.11320754885673523
            },
            {
                'latitude': -34.48154830932617,
                'point_count': 3176,
                'longitude_origin': 0.0,
                'longitude_step': 0.11335012316703796
            },
            {
                'latitude': -34.55184555053711,
                'point_count': 3172,
                'longitude_origin': 0.0,
                'longitude_step': 0.11349306255578995
            },
            {
                'latitude': -34.62214279174805,
                'point_count': 3168,
                'longitude_origin': 0.0,
                'longitude_step': 0.11363636702299118
            },
            {
                'latitude': -34.69244384765625,
                'point_count': 3164,
                'longitude_origin': 0.0,
                'longitude_step': 0.11378002166748047
            },
            {
                'latitude': -34.76274108886719,
                'point_count': 3160,
                'longitude_origin': 0.0,
                'longitude_step': 0.1139240488409996
            },
            {
                'latitude': -34.83304214477539,
                'point_count': 3156,
                'longitude_origin': 0.0,
                'longitude_step': 0.11406844109296799
            },
            {
                'latitude': -34.90333938598633,
                'point_count': 3152,
                'longitude_origin': 0.0,
                'longitude_step': 0.11421319842338562
            },
            {
                'latitude': -34.973636627197266,
                'point_count': 3148,
                'longitude_origin': 0.0,
                'longitude_step': 0.1143583208322525
            },
            {
                'latitude': -35.04393768310547,
                'point_count': 3144,
                'longitude_origin': 0.0,
                'longitude_step': 0.11450381577014923
            },
            {
                'latitude': -35.114234924316406,
                'point_count': 3140,
                'longitude_origin': 0.0,
                'longitude_step': 0.1146496832370758
            },
            {
                'latitude': -35.18453598022461,
                'point_count': 3136,
                'longitude_origin': 0.0,
                'longitude_step': 0.11479591578245163
            },
            {
                'latitude': -35.25483322143555,
                'point_count': 3132,
                'longitude_origin': 0.0,
                'longitude_step': 0.1149425283074379
            },
            {
                'latitude': -35.325130462646484,
                'point_count': 3128,
                'longitude_origin': 0.0,
                'longitude_step': 0.11508951336145401
            },
            {
                'latitude': -35.39543151855469,
                'point_count': 3124,
                'longitude_origin': 0.0,
                'longitude_step': 0.11523687839508057
            },
            {
                'latitude': -35.465728759765625,
                'point_count': 3120,
                'longitude_origin': 0.0,
                'longitude_step': 0.11538461595773697
            },
            {
                'latitude': -35.53602981567383,
                'point_count': 3116,
                'longitude_origin': 0.0,
                'longitude_step': 0.11553273350000381
            },
            {
                'latitude': -35.606327056884766,
                'point_count': 3112,
                'longitude_origin': 0.0,
                'longitude_step': 0.1156812310218811
            },
            {
                'latitude': -35.6766242980957,
                'point_count': 3108,
                'longitude_origin': 0.0,
                'longitude_step': 0.11583011597394943
            },
            {
                'latitude': -35.746925354003906,
                'point_count': 3104,
                'longitude_origin': 0.0,
                'longitude_step': 0.1159793809056282
            },
            {
                'latitude': -35.817222595214844,
                'point_count': 3100,
                'longitude_origin': 0.0,
                'longitude_step': 0.11612903326749802
            },
            {
                'latitude': -35.88752365112305,
                'point_count': 3096,
                'longitude_origin': 0.0,
                'longitude_step': 0.11627907305955887
            },
            {
                'latitude': -35.957820892333984,
                'point_count': 3092,
                'longitude_origin': 0.0,
                'longitude_step': 0.11642949283123016
            },
            {
                'latitude': -36.02811813354492,
                'point_count': 3088,
                'longitude_origin': 0.0,
                'longitude_step': 0.1165803074836731
            },
            {
                'latitude': -36.098419189453125,
                'point_count': 3084,
                'longitude_origin': 0.0,
                'longitude_step': 0.11673151701688766
            },
            {
                'latitude': -36.16871643066406,
                'point_count': 3080,
                'longitude_origin': 0.0,
                'longitude_step': 0.11688311398029327
            },
            {
                'latitude': -36.239017486572266,
                'point_count': 3076,
                'longitude_origin': 0.0,
                'longitude_step': 0.11703511327505112
            },
            {
                'latitude': -36.3093147277832,
                'point_count': 3072,
                'longitude_origin': 0.0,
                'longitude_step': 0.1171875
            },
            {
                'latitude': -36.37961196899414,
                'point_count': 3068,
                'longitude_origin': 0.0,
                'longitude_step': 0.11734028905630112
            },
            {
                'latitude': -36.449913024902344,
                'point_count': 3064,
                'longitude_origin': 0.0,
                'longitude_step': 0.11749347299337387
            },
            {
                'latitude': -36.52021026611328,
                'point_count': 3060,
                'longitude_origin': 0.0,
                'longitude_step': 0.11764705926179886
            },
            {
                'latitude': -36.590511322021484,
                'point_count': 3056,
                'longitude_origin': 0.0,
                'longitude_step': 0.11780104786157608
            },
            {
                'latitude': -36.66080856323242,
                'point_count': 3052,
                'longitude_origin': 0.0,
                'longitude_step': 0.11795543879270554
            },
            {
                'latitude': -36.73110580444336,
                'point_count': 3048,
                'longitude_origin': 0.0,
                'longitude_step': 0.11811023950576782
            },
            {
                'latitude': -36.80140686035156,
                'point_count': 3044,
                'longitude_origin': 0.0,
                'longitude_step': 0.11826544255018234
            },
            {
                'latitude': -36.8717041015625,
                'point_count': 3040,
                'longitude_origin': 0.0,
                'longitude_step': 0.1184210553765297
            },
            {
                'latitude': -36.9420051574707,
                'point_count': 3036,
                'longitude_origin': 0.0,
                'longitude_step': 0.11857707798480988
            },
            {
                'latitude': -37.01230239868164,
                'point_count': 3032,
                'longitude_origin': 0.0,
                'longitude_step': 0.11873351037502289
            },
            {
                'latitude': -37.08259963989258,
                'point_count': 3028,
                'longitude_origin': 0.0,
                'longitude_step': 0.11889035999774933
            },
            {
                'latitude': -37.15290069580078,
                'point_count': 3024,
                'longitude_origin': 0.0,
                'longitude_step': 0.1190476194024086
            },
            {
                'latitude': -37.22319793701172,
                'point_count': 3020,
                'longitude_origin': 0.0,
                'longitude_step': 0.1192052960395813
            },
            {
                'latitude': -37.29349899291992,
                'point_count': 3016,
                'longitude_origin': 0.0,
                'longitude_step': 0.11936339735984802
            },
            {
                'latitude': -37.36379623413086,
                'point_count': 3012,
                'longitude_origin': 0.0,
                'longitude_step': 0.11952191591262817
            },
            {
                'latitude': -37.4340934753418,
                'point_count': 3008,
                'longitude_origin': 0.0,
                'longitude_step': 0.11968085169792175
            },
            {
                'latitude': -37.50439453125,
                'point_count': 3004,
                'longitude_origin': 0.0,
                'longitude_step': 0.11984021216630936
            },
            {
                'latitude': -37.57469177246094,
                'point_count': 3000,
                'longitude_origin': 0.0,
                'longitude_step': 0.11999999731779099
            },
            {
                'latitude': -37.64499282836914,
                'point_count': 2996,
                'longitude_origin': 0.0,
                'longitude_step': 0.12016021460294724
            },
            {
                'latitude': -37.71529006958008,
                'point_count': 2992,
                'longitude_origin': 0.0,
                'longitude_step': 0.12032085657119751
            },
            {
                'latitude': -37.785587310791016,
                'point_count': 2988,
                'longitude_origin': 0.0,
                'longitude_step': 0.1204819306731224
            },
            {
                'latitude': -37.85588836669922,
                'point_count': 2984,
                'longitude_origin': 0.0,
                'longitude_step': 0.12064342945814133
            },
            {
                'latitude': -37.926185607910156,
                'point_count': 2980,
                'longitude_origin': 0.0,
                'longitude_step': 0.12080536782741547
            },
            {
                'latitude': -37.99648666381836,
                'point_count': 2976,
                'longitude_origin': 0.0,
                'longitude_step': 0.12096773833036423
            },
            {
                'latitude': -38.0667839050293,
                'point_count': 2972,
                'longitude_origin': 0.0,
                'longitude_step': 0.1211305484175682
            },
            {
                'latitude': -38.137081146240234,
                'point_count': 2968,
                'longitude_origin': 0.0,
                'longitude_step': 0.1212937980890274
            },
            {
                'latitude': -38.20738220214844,
                'point_count': 2964,
                'longitude_origin': 0.0,
                'longitude_step': 0.12145748734474182
            },
            {
                'latitude': -38.277679443359375,
                'point_count': 2960,
                'longitude_origin': 0.0,
                'longitude_step': 0.12162162363529205
            },
            {
                'latitude': -38.34798049926758,
                'point_count': 2956,
                'longitude_origin': 0.0,
                'longitude_step': 0.1217861995100975
            },
            {
                'latitude': -38.418277740478516,
                'point_count': 2952,
                'longitude_origin': 0.0,
                'longitude_step': 0.12195122241973877
            },
            {
                'latitude': -38.48857498168945,
                'point_count': 2948,
                'longitude_origin': 0.0,
                'longitude_step': 0.12211669236421585
            },
            {
                'latitude': -38.558876037597656,
                'point_count': 2944,
                'longitude_origin': 0.0,
                'longitude_step': 0.12228260934352875
            },
            {
                'latitude': -38.629173278808594,
                'point_count': 2940,
                'longitude_origin': 0.0,
                'longitude_step': 0.12244898080825806
            },
            {
                'latitude': -38.6994743347168,
                'point_count': 2936,
                'longitude_origin': 0.0,
                'longitude_step': 0.12261580675840378
            },
            {
                'latitude': -38.769771575927734,
                'point_count': 2932,
                'longitude_origin': 0.0,
                'longitude_step': 0.12278307974338531
            },
            {
                'latitude': -38.84006881713867,
                'point_count': 2928,
                'longitude_origin': 0.0,
                'longitude_step': 0.12295082211494446
            },
            {
                'latitude': -38.910369873046875,
                'point_count': 2924,
                'longitude_origin': 0.0,
                'longitude_step': 0.12311901152133942
            },
            {
                'latitude': -38.98066711425781,
                'point_count': 2920,
                'longitude_origin': 0.0,
                'longitude_step': 0.12328767031431198
            },
            {
                'latitude': -39.050968170166016,
                'point_count': 2916,
                'longitude_origin': 0.0,
                'longitude_step': 0.12345679104328156
            },
            {
                'latitude': -39.12126541137695,
                'point_count': 2912,
                'longitude_origin': 0.0,
                'longitude_step': 0.12362637370824814
            },
            {
                'latitude': -39.19156265258789,
                'point_count': 2908,
                'longitude_origin': 0.0,
                'longitude_step': 0.12379642575979233
            },
            {
                'latitude': -39.261863708496094,
                'point_count': 2904,
                'longitude_origin': 0.0,
                'longitude_step': 0.12396693974733353
            },
            {
                'latitude': -39.33216094970703,
                'point_count': 2900,
                'longitude_origin': 0.0,
                'longitude_step': 0.12413793057203293
            },
            {
                'latitude': -39.402462005615234,
                'point_count': 2896,
                'longitude_origin': 0.0,
                'longitude_step': 0.12430939078330994
            },
            {
                'latitude': -39.47275924682617,
                'point_count': 2892,
                'longitude_origin': 0.0,
                'longitude_step': 0.12448132783174515
            },
            {
                'latitude': -39.54305648803711,
                'point_count': 2888,
                'longitude_origin': 0.0,
                'longitude_step': 0.12465374171733856
            },
            {
                'latitude': -39.61335754394531,
                'point_count': 2884,
                'longitude_origin': 0.0,
                'longitude_step': 0.12482663244009018
            },
            {
                'latitude': -39.68365478515625,
                'point_count': 2880,
                'longitude_origin': 0.0,
                'longitude_step': 0.125
            },
            {
                'latitude': -39.75395584106445,
                'point_count': 2876,
                'longitude_origin': 0.0,
                'longitude_step': 0.12517385184764862
            },
            {
                'latitude': -39.82425308227539,
                'point_count': 2872,
                'longitude_origin': 0.0,
                'longitude_step': 0.12534819543361664
            },
            {
                'latitude': -39.89455032348633,
                'point_count': 2868,
                'longitude_origin': 0.0,
                'longitude_step': 0.12552301585674286
            },
            {
                'latitude': -39.96485137939453,
                'point_count': 2864,
                'longitude_origin': 0.0,
                'longitude_step': 0.12569832801818848
            },
            {
                'latitude': -40.03514862060547,
                'point_count': 2860,
                'longitude_origin': 0.0,
                'longitude_step': 0.1258741319179535
            },
            {
                'latitude': -40.10544967651367,
                'point_count': 2856,
                'longitude_origin': 0.0,
                'longitude_step': 0.1260504275560379
            },
            {
                'latitude': -40.17574691772461,
                'point_count': 2852,
                'longitude_origin': 0.0,
                'longitude_step': 0.1262272149324417
            },
            {
                'latitude': -40.24604415893555,
                'point_count': 2848,
                'longitude_origin': 0.0,
                'longitude_step': 0.12640449404716492
            },
            {
                'latitude': -40.31634521484375,
                'point_count': 2844,
                'longitude_origin': 0.0,
                'longitude_step': 0.1265822798013687
            },
            {
                'latitude': -40.38664245605469,
                'point_count': 2840,
                'longitude_origin': 0.0,
                'longitude_step': 0.1267605572938919
            },
            {
                'latitude': -40.45694351196289,
                'point_count': 2836,
                'longitude_origin': 0.0,
                'longitude_step': 0.12693935632705688
            },
            {
                'latitude': -40.52724075317383,
                'point_count': 2832,
                'longitude_origin': 0.0,
                'longitude_step': 0.12711864709854126
            },
            {
                'latitude': -40.597537994384766,
                'point_count': 2828,
                'longitude_origin': 0.0,
                'longitude_step': 0.12729844450950623
            },
            {
                'latitude': -40.66783905029297,
                'point_count': 2824,
                'longitude_origin': 0.0,
                'longitude_step': 0.12747874855995178
            },
            {
                'latitude': -40.738136291503906,
                'point_count': 2820,
                'longitude_origin': 0.0,
                'longitude_step': 0.12765957415103912
            },
            {
                'latitude': -40.80843734741211,
                'point_count': 2816,
                'longitude_origin': 0.0,
                'longitude_step': 0.12784090638160706
            },
            {
                'latitude': -40.87873458862305,
                'point_count': 2812,
                'longitude_origin': 0.0,
                'longitude_step': 0.12802276015281677
            },
            {
                'latitude': -40.949031829833984,
                'point_count': 2808,
                'longitude_origin': 0.0,
                'longitude_step': 0.12820513546466827
            },
            {
                'latitude': -41.01933288574219,
                'point_count': 2804,
                'longitude_origin': 0.0,
                'longitude_step': 0.12838801741600037
            },
            {
                'latitude': -41.089630126953125,
                'point_count': 2800,
                'longitude_origin': 0.0,
                'longitude_step': 0.12857143580913544
            },
            {
                'latitude': -41.15993118286133,
                'point_count': 2796,
                'longitude_origin': 0.0,
                'longitude_step': 0.1287553608417511
            },
            {
                'latitude': -41.230228424072266,
                'point_count': 2792,
                'longitude_origin': 0.0,
                'longitude_step': 0.12893982231616974
            },
            {
                'latitude': -41.3005256652832,
                'point_count': 2788,
                'longitude_origin': 0.0,
                'longitude_step': 0.12912482023239136
            },
            {
                'latitude': -41.370826721191406,
                'point_count': 2784,
                'longitude_origin': 0.0,
                'longitude_step': 0.12931033968925476
            },
            {
                'latitude': -41.441123962402344,
                'point_count': 2780,
                'longitude_origin': 0.0,
                'longitude_step': 0.12949639558792114
            },
            {
                'latitude': -41.51142501831055,
                'point_count': 2776,
                'longitude_origin': 0.0,
                'longitude_step': 0.1296830028295517
            },
            {
                'latitude': -41.581722259521484,
                'point_count': 2772,
                'longitude_origin': 0.0,
                'longitude_step': 0.12987013161182404
            },
            {
                'latitude': -41.65201950073242,
                'point_count': 2768,
                'longitude_origin': 0.0,
                'longitude_step': 0.13005779683589935
            },
            {
                'latitude': -41.722320556640625,
                'point_count': 2764,
                'longitude_origin': 0.0,
                'longitude_step': 0.13024601340293884
            },
            {
                'latitude': -41.79261779785156,
                'point_count': 2760,
                'longitude_origin': 0.0,
                'longitude_step': 0.1304347813129425
            },
            {
                'latitude': -41.862918853759766,
                'point_count': 2756,
                'longitude_origin': 0.0,
                'longitude_step': 0.13062408566474915
            },
            {
                'latitude': -41.9332160949707,
                'point_count': 2752,
                'longitude_origin': 0.0,
                'longitude_step': 0.13081395626068115
            },
            {
                'latitude': -42.00351333618164,
                'point_count': 2748,
                'longitude_origin': 0.0,
                'longitude_step': 0.13100436329841614
            },
            {
                'latitude': -42.073814392089844,
                'point_count': 2744,
                'longitude_origin': 0.0,
                'longitude_step': 0.1311953365802765
            },
            {
                'latitude': -42.14411163330078,
                'point_count': 2740,
                'longitude_origin': 0.0,
                'longitude_step': 0.131386861205101
            },
            {
                'latitude': -42.214412689208984,
                'point_count': 2736,
                'longitude_origin': 0.0,
                'longitude_step': 0.1315789520740509
            },
            {
                'latitude': -42.28470993041992,
                'point_count': 2732,
                'longitude_origin': 0.0,
                'longitude_step': 0.13177159428596497
            },
            {
                'latitude': -42.35500717163086,
                'point_count': 2728,
                'longitude_origin': 0.0,
                'longitude_step': 0.1319648027420044
            },
            {
                'latitude': -42.42530822753906,
                'point_count': 2724,
                'longitude_origin': 0.0,
                'longitude_step': 0.13215859234333038
            },
            {
                'latitude': -42.49560546875,
                'point_count': 2720,
                'longitude_origin': 0.0,
                'longitude_step': 0.13235294818878174
            },
            {
                'latitude': -42.5659065246582,
                'point_count': 2716,
                'longitude_origin': 0.0,
                'longitude_step': 0.13254787027835846
            },
            {
                'latitude': -42.63620376586914,
                'point_count': 2712,
                'longitude_origin': 0.0,
                'longitude_step': 0.13274335861206055
            },
            {
                'latitude': -42.70650100708008,
                'point_count': 2708,
                'longitude_origin': 0.0,
                'longitude_step': 0.1329394429922104
            },
            {
                'latitude': -42.77680206298828,
                'point_count': 2704,
                'longitude_origin': 0.0,
                'longitude_step': 0.1331360936164856
            },
            {
                'latitude': -42.84709930419922,
                'point_count': 2700,
                'longitude_origin': 0.0,
                'longitude_step': 0.13333334028720856
            },
            {
                'latitude': -42.91740036010742,
                'point_count': 2696,
                'longitude_origin': 0.0,
                'longitude_step': 0.13353115320205688
            },
            {
                'latitude': -42.98769760131836,
                'point_count': 2692,
                'longitude_origin': 0.0,
                'longitude_step': 0.13372956216335297
            },
            {
                'latitude': -43.0579948425293,
                'point_count': 2688,
                'longitude_origin': 0.0,
                'longitude_step': 0.1339285671710968
            },
            {
                'latitude': -43.1282958984375,
                'point_count': 2684,
                'longitude_origin': 0.0,
                'longitude_step': 0.1341281682252884
            },
            {
                'latitude': -43.19859313964844,
                'point_count': 2680,
                'longitude_origin': 0.0,
                'longitude_step': 0.13432836532592773
            },
            {
                'latitude': -43.26889419555664,
                'point_count': 2676,
                'longitude_origin': 0.0,
                'longitude_step': 0.13452914357185364
            },
            {
                'latitude': -43.33919143676758,
                'point_count': 2672,
                'longitude_origin': 0.0,
                'longitude_step': 0.1347305327653885
            },
            {
                'latitude': -43.409488677978516,
                'point_count': 2668,
                'longitude_origin': 0.0,
                'longitude_step': 0.1349325329065323
            },
            {
                'latitude': -43.47978973388672,
                'point_count': 2664,
                'longitude_origin': 0.0,
                'longitude_step': 0.13513512909412384
            },
            {
                'latitude': -43.550086975097656,
                'point_count': 2660,
                'longitude_origin': 0.0,
                'longitude_step': 0.13533835113048553
            },
            {
                'latitude': -43.62038803100586,
                'point_count': 2656,
                'longitude_origin': 0.0,
                'longitude_step': 0.13554216921329498
            },
            {
                'latitude': -43.6906852722168,
                'point_count': 2652,
                'longitude_origin': 0.0,
                'longitude_step': 0.13574661314487457
            },
            {
                'latitude': -43.760982513427734,
                'point_count': 2648,
                'longitude_origin': 0.0,
                'longitude_step': 0.1359516680240631
            },
            {
                'latitude': -43.83128356933594,
                'point_count': 2644,
                'longitude_origin': 0.0,
                'longitude_step': 0.1361573338508606
            },
            {
                'latitude': -43.901580810546875,
                'point_count': 2640,
                'longitude_origin': 0.0,
                'longitude_step': 0.13636364042758942
            },
            {
                'latitude': -43.97188186645508,
                'point_count': 2636,
                'longitude_origin': 0.0,
                'longitude_step': 0.13657055795192719
            },
            {
                'latitude': -44.042179107666016,
                'point_count': 2632,
                'longitude_origin': 0.0,
                'longitude_step': 0.1367781162261963
            },
            {
                'latitude': -44.11247634887695,
                'point_count': 2628,
                'longitude_origin': 0.0,
                'longitude_step': 0.13698630034923553
            },
            {
                'latitude': -44.182777404785156,
                'point_count': 2624,
                'longitude_origin': 0.0,
                'longitude_step': 0.13719512522220612
            },
            {
                'latitude': -44.253074645996094,
                'point_count': 2620,
                'longitude_origin': 0.0,
                'longitude_step': 0.13740457594394684
            },
            {
                'latitude': -44.3233757019043,
                'point_count': 2616,
                'longitude_origin': 0.0,
                'longitude_step': 0.1376146823167801
            },
            {
                'latitude': -44.393672943115234,
                'point_count': 2612,
                'longitude_origin': 0.0,
                'longitude_step': 0.13782541453838348
            },
            {
                'latitude': -44.46397018432617,
                'point_count': 2608,
                'longitude_origin': 0.0,
                'longitude_step': 0.1380368024110794
            },
            {
                'latitude': -44.534271240234375,
                'point_count': 2604,
                'longitude_origin': 0.0,
                'longitude_step': 0.13824884593486786
            },
            {
                'latitude': -44.60456848144531,
                'point_count': 2600,
                'longitude_origin': 0.0,
                'longitude_step': 0.13846154510974884
            },
            {
                'latitude': -44.674869537353516,
                'point_count': 2596,
                'longitude_origin': 0.0,
                'longitude_step': 0.13867488503456116
            },
            {
                'latitude': -44.74516677856445,
                'point_count': 2592,
                'longitude_origin': 0.0,
                'longitude_step': 0.1388888955116272
            },
            {
                'latitude': -44.81546401977539,
                'point_count': 2588,
                'longitude_origin': 0.0,
                'longitude_step': 0.13910356163978577
            },
            {
                'latitude': -44.885765075683594,
                'point_count': 2584,
                'longitude_origin': 0.0,
                'longitude_step': 0.13931888341903687
            },
            {
                'latitude': -44.95606231689453,
                'point_count': 2580,
                'longitude_origin': 0.0,
                'longitude_step': 0.13953489065170288
            },
            {
                'latitude': -45.026363372802734,
                'point_count': 2576,
                'longitude_origin': 0.0,
                'longitude_step': 0.13975155353546143
            },
            {
                'latitude': -45.09666061401367,
                'point_count': 2572,
                'longitude_origin': 0.0,
                'longitude_step': 0.1399689018726349
            },
            {
                'latitude': -45.16695785522461,
                'point_count': 2568,
                'longitude_origin': 0.0,
                'longitude_step': 0.14018692076206207
            },
            {
                'latitude': -45.23725891113281,
                'point_count': 2564,
                'longitude_origin': 0.0,
                'longitude_step': 0.14040561020374298
            },
            {
                'latitude': -45.30755615234375,
                'point_count': 2560,
                'longitude_origin': 0.0,
                'longitude_step': 0.140625
            },
            {
                'latitude': -45.37785720825195,
                'point_count': 2556,
                'longitude_origin': 0.0,
                'longitude_step': 0.14084507524967194
            },
            {
                'latitude': -45.44815444946289,
                'point_count': 2552,
                'longitude_origin': 0.0,
                'longitude_step': 0.1410658359527588
            },
            {
                'latitude': -45.51845169067383,
                'point_count': 2548,
                'longitude_origin': 0.0,
                'longitude_step': 0.14128728210926056
            },
            {
                'latitude': -45.58875274658203,
                'point_count': 2544,
                'longitude_origin': 0.0,
                'longitude_step': 0.14150942862033844
            },
            {
                'latitude': -45.65904998779297,
                'point_count': 2540,
                'longitude_origin': 0.0,
                'longitude_step': 0.14173229038715363
            },
            {
                'latitude': -45.72935104370117,
                'point_count': 2536,
                'longitude_origin': 0.0,
                'longitude_step': 0.14195583760738373
            },
            {
                'latitude': -45.79964828491211,
                'point_count': 2532,
                'longitude_origin': 0.0,
                'longitude_step': 0.14218010008335114
            },
            {
                'latitude': -45.86994552612305,
                'point_count': 2528,
                'longitude_origin': 0.0,
                'longitude_step': 0.14240506291389465
            },
            {
                'latitude': -45.94024658203125,
                'point_count': 2524,
                'longitude_origin': 0.0,
                'longitude_step': 0.14263074100017548
            },
            {
                'latitude': -46.01054382324219,
                'point_count': 2520,
                'longitude_origin': 0.0,
                'longitude_step': 0.1428571492433548
            },
            {
                'latitude': -46.08084487915039,
                'point_count': 2516,
                'longitude_origin': 0.0,
                'longitude_step': 0.14308425784111023
            },
            {
                'latitude': -46.15114212036133,
                'point_count': 2512,
                'longitude_origin': 0.0,
                'longitude_step': 0.14331209659576416
            },
            {
                'latitude': -46.221439361572266,
                'point_count': 2508,
                'longitude_origin': 0.0,
                'longitude_step': 0.1435406655073166
            },
            {
                'latitude': -46.29174041748047,
                'point_count': 2504,
                'longitude_origin': 0.0,
                'longitude_step': 0.14376996457576752
            },
            {
                'latitude': -46.362037658691406,
                'point_count': 2500,
                'longitude_origin': 0.0,
                'longitude_step': 0.14399999380111694
            },
            {
                'latitude': -46.43233871459961,
                'point_count': 2496,
                'longitude_origin': 0.0,
                'longitude_step': 0.14423076808452606
            },
            {
                'latitude': -46.50263595581055,
                'point_count': 2492,
                'longitude_origin': 0.0,
                'longitude_step': 0.14446227252483368
            },
            {
                'latitude': -46.572933197021484,
                'point_count': 2488,
                'longitude_origin': 0.0,
                'longitude_step': 0.14469453692436218
            },
            {
                'latitude': -46.64323425292969,
                'point_count': 2484,
                'longitude_origin': 0.0,
                'longitude_step': 0.14492753148078918
            },
            {
                'latitude': -46.713531494140625,
                'point_count': 2480,
                'longitude_origin': 0.0,
                'longitude_step': 0.14516128599643707
            },
            {
                'latitude': -46.78383255004883,
                'point_count': 2476,
                'longitude_origin': 0.0,
                'longitude_step': 0.14539580047130585
            },
            {
                'latitude': -46.854129791259766,
                'point_count': 2472,
                'longitude_origin': 0.0,
                'longitude_step': 0.1456310749053955
            },
            {
                'latitude': -46.9244270324707,
                'point_count': 2468,
                'longitude_origin': 0.0,
                'longitude_step': 0.14586709439754486
            },
            {
                'latitude': -46.994728088378906,
                'point_count': 2464,
                'longitude_origin': 0.0,
                'longitude_step': 0.1461038887500763
            },
            {
                'latitude': -47.065025329589844,
                'point_count': 2460,
                'longitude_origin': 0.0,
                'longitude_step': 0.1463414579629898
            },
            {
                'latitude': -47.13532638549805,
                'point_count': 2456,
                'longitude_origin': 0.0,
                'longitude_step': 0.1465798020362854
            },
            {
                'latitude': -47.205623626708984,
                'point_count': 2452,
                'longitude_origin': 0.0,
                'longitude_step': 0.14681892096996307
            },
            {
                'latitude': -47.27592086791992,
                'point_count': 2448,
                'longitude_origin': 0.0,
                'longitude_step': 0.14705882966518402
            },
            {
                'latitude': -47.346221923828125,
                'point_count': 2444,
                'longitude_origin': 0.0,
                'longitude_step': 0.14729951322078705
            },
            {
                'latitude': -47.41651916503906,
                'point_count': 2440,
                'longitude_origin': 0.0,
                'longitude_step': 0.14754098653793335
            },
            {
                'latitude': -47.486820220947266,
                'point_count': 2436,
                'longitude_origin': 0.0,
                'longitude_step': 0.14778324961662292
            },
            {
                'latitude': -47.5571174621582,
                'point_count': 2432,
                'longitude_origin': 0.0,
                'longitude_step': 0.14802631735801697
            },
            {
                'latitude': -47.62741470336914,
                'point_count': 2428,
                'longitude_origin': 0.0,
                'longitude_step': 0.14827017486095428
            },
            {
                'latitude': -47.697715759277344,
                'point_count': 2424,
                'longitude_origin': 0.0,
                'longitude_step': 0.14851485192775726
            },
            {
                'latitude': -47.76801300048828,
                'point_count': 2420,
                'longitude_origin': 0.0,
                'longitude_step': 0.1487603336572647
            },
            {
                'latitude': -47.838314056396484,
                'point_count': 2416,
                'longitude_origin': 0.0,
                'longitude_step': 0.14900662004947662
            },
            {
                'latitude': -47.90861129760742,
                'point_count': 2412,
                'longitude_origin': 0.0,
                'longitude_step': 0.1492537260055542
            },
            {
                'latitude': -47.97890853881836,
                'point_count': 2408,
                'longitude_origin': 0.0,
                'longitude_step': 0.14950166642665863
            },
            {
                'latitude': -48.04920959472656,
                'point_count': 2404,
                'longitude_origin': 0.0,
                'longitude_step': 0.14975041151046753
            },
            {
                'latitude': -48.1195068359375,
                'point_count': 2400,
                'longitude_origin': 0.0,
                'longitude_step': 0.15000000596046448
            },
            {
                'latitude': -48.1898078918457,
                'point_count': 2396,
                'longitude_origin': 0.0,
                'longitude_step': 0.1502504199743271
            },
            {
                'latitude': -48.26010513305664,
                'point_count': 2392,
                'longitude_origin': 0.0,
                'longitude_step': 0.15050166845321655
            },
            {
                'latitude': -48.33040237426758,
                'point_count': 2388,
                'longitude_origin': 0.0,
                'longitude_step': 0.15075376629829407
            },
            {
                'latitude': -48.40070343017578,
                'point_count': 2384,
                'longitude_origin': 0.0,
                'longitude_step': 0.15100671350955963
            },
            {
                'latitude': -48.47100067138672,
                'point_count': 2380,
                'longitude_origin': 0.0,
                'longitude_step': 0.15126051008701324
            },
            {
                'latitude': -48.54130172729492,
                'point_count': 2376,
                'longitude_origin': 0.0,
                'longitude_step': 0.1515151560306549
            },
            {
                'latitude': -48.61159896850586,
                'point_count': 2372,
                'longitude_origin': 0.0,
                'longitude_step': 0.15177065134048462
            },
            {
                'latitude': -48.6818962097168,
                'point_count': 2368,
                'longitude_origin': 0.0,
                'longitude_step': 0.15202702581882477
            },
            {
                'latitude': -48.752197265625,
                'point_count': 2364,
                'longitude_origin': 0.0,
                'longitude_step': 0.15228426456451416
            },
            {
                'latitude': -48.82249450683594,
                'point_count': 2360,
                'longitude_origin': 0.0,
                'longitude_step': 0.1525423675775528
            },
            {
                'latitude': -48.89279556274414,
                'point_count': 2356,
                'longitude_origin': 0.0,
                'longitude_step': 0.15280136466026306
            },
            {
                'latitude': -48.96309280395508,
                'point_count': 2352,
                'longitude_origin': 0.0,
                'longitude_step': 0.15306122601032257
            },
            {
                'latitude': -49.033390045166016,
                'point_count': 2348,
                'longitude_origin': 0.0,
                'longitude_step': 0.1533219814300537
            },
            {
                'latitude': -49.10369110107422,
                'point_count': 2344,
                'longitude_origin': 0.0,
                'longitude_step': 0.1535836160182953
            },
            {
                'latitude': -49.173988342285156,
                'point_count': 2340,
                'longitude_origin': 0.0,
                'longitude_step': 0.1538461595773697
            },
            {
                'latitude': -49.24428939819336,
                'point_count': 2336,
                'longitude_origin': 0.0,
                'longitude_step': 0.15410958230495453
            },
            {
                'latitude': -49.3145866394043,
                'point_count': 2332,
                'longitude_origin': 0.0,
                'longitude_step': 0.1543739289045334
            },
            {
                'latitude': -49.384883880615234,
                'point_count': 2328,
                'longitude_origin': 0.0,
                'longitude_step': 0.15463916957378387
            },
            {
                'latitude': -49.45518493652344,
                'point_count': 2324,
                'longitude_origin': 0.0,
                'longitude_step': 0.15490533411502838
            },
            {
                'latitude': -49.525482177734375,
                'point_count': 2320,
                'longitude_origin': 0.0,
                'longitude_step': 0.1551724076271057
            },
            {
                'latitude': -49.59578323364258,
                'point_count': 2316,
                'longitude_origin': 0.0,
                'longitude_step': 0.15544041991233826
            },
            {
                'latitude': -49.666080474853516,
                'point_count': 2312,
                'longitude_origin': 0.0,
                'longitude_step': 0.15570934116840363
            },
            {
                'latitude': -49.73637771606445,
                'point_count': 2308,
                'longitude_origin': 0.0,
                'longitude_step': 0.1559792011976242
            },
            {
                'latitude': -49.806678771972656,
                'point_count': 2304,
                'longitude_origin': 0.0,
                'longitude_step': 0.15625
            },
            {
                'latitude': -49.876976013183594,
                'point_count': 2300,
                'longitude_origin': 0.0,
                'longitude_step': 0.156521737575531
            },
            {
                'latitude': -49.9472770690918,
                'point_count': 2296,
                'longitude_origin': 0.0,
                'longitude_step': 0.15679442882537842
            },
            {
                'latitude': -50.017574310302734,
                'point_count': 2292,
                'longitude_origin': 0.0,
                'longitude_step': 0.15706805884838104
            },
            {
                'latitude': -50.08787155151367,
                'point_count': 2288,
                'longitude_origin': 0.0,
                'longitude_step': 0.15734265744686127
            },
            {
                'latitude': -50.158172607421875,
                'point_count': 2284,
                'longitude_origin': 0.0,
                'longitude_step': 0.1576182097196579
            },
            {
                'latitude': -50.22846984863281,
                'point_count': 2280,
                'longitude_origin': 0.0,
                'longitude_step': 0.15789473056793213
            },
            {
                'latitude': -50.298770904541016,
                'point_count': 2276,
                'longitude_origin': 0.0,
                'longitude_step': 0.15817223489284515
            },
            {
                'latitude': -50.36906814575195,
                'point_count': 2272,
                'longitude_origin': 0.0,
                'longitude_step': 0.15845070779323578
            },
            {
                'latitude': -50.43936538696289,
                'point_count': 2268,
                'longitude_origin': 0.0,
                'longitude_step': 0.1587301641702652
            },
            {
                'latitude': -50.509666442871094,
                'point_count': 2264,
                'longitude_origin': 0.0,
                'longitude_step': 0.1590106040239334
            },
            {
                'latitude': -50.57996368408203,
                'point_count': 2260,
                'longitude_origin': 0.0,
                'longitude_step': 0.1592920422554016
            },
            {
                'latitude': -50.650264739990234,
                'point_count': 2256,
                'longitude_origin': 0.0,
                'longitude_step': 0.1595744639635086
            },
            {
                'latitude': -50.72056198120117,
                'point_count': 2252,
                'longitude_origin': 0.0,
                'longitude_step': 0.15985789895057678
            },
            {
                'latitude': -50.79085922241211,
                'point_count': 2248,
                'longitude_origin': 0.0,
                'longitude_step': 0.16014234721660614
            },
            {
                'latitude': -50.86116027832031,
                'point_count': 2244,
                'longitude_origin': 0.0,
                'longitude_step': 0.16042780876159668
            },
            {
                'latitude': -50.93145751953125,
                'point_count': 2240,
                'longitude_origin': 0.0,
                'longitude_step': 0.1607142835855484
            },
            {
                'latitude': -51.00175857543945,
                'point_count': 2236,
                'longitude_origin': 0.0,
                'longitude_step': 0.1610017865896225
            },
            {
                'latitude': -51.07205581665039,
                'point_count': 2232,
                'longitude_origin': 0.0,
                'longitude_step': 0.16129031777381897
            },
            {
                'latitude': -51.14235305786133,
                'point_count': 2228,
                'longitude_origin': 0.0,
                'longitude_step': 0.161579892039299
            },
            {
                'latitude': -51.21265411376953,
                'point_count': 2224,
                'longitude_origin': 0.0,
                'longitude_step': 0.16187050938606262
            },
            {
                'latitude': -51.28295135498047,
                'point_count': 2220,
                'longitude_origin': 0.0,
                'longitude_step': 0.1621621549129486
            },
            {
                'latitude': -51.35325241088867,
                'point_count': 2216,
                'longitude_origin': 0.0,
                'longitude_step': 0.16245487332344055
            },
            {
                'latitude': -51.42354965209961,
                'point_count': 2212,
                'longitude_origin': 0.0,
                'longitude_step': 0.16274864971637726
            },
            {
                'latitude': -51.49384689331055,
                'point_count': 2208,
                'longitude_origin': 0.0,
                'longitude_step': 0.16304348409175873
            },
            {
                'latitude': -51.56414794921875,
                'point_count': 2204,
                'longitude_origin': 0.0,
                'longitude_step': 0.16333937644958496
            },
            {
                'latitude': -51.63444519042969,
                'point_count': 2200,
                'longitude_origin': 0.0,
                'longitude_step': 0.16363635659217834
            },
            {
                'latitude': -51.70474624633789,
                'point_count': 2196,
                'longitude_origin': 0.0,
                'longitude_step': 0.16393442451953888
            },
            {
                'latitude': -51.77504348754883,
                'point_count': 2192,
                'longitude_origin': 0.0,
                'longitude_step': 0.16423358023166656
            },
            {
                'latitude': -51.845340728759766,
                'point_count': 2188,
                'longitude_origin': 0.0,
                'longitude_step': 0.1645338237285614
            },
            {
                'latitude': -51.91564178466797,
                'point_count': 2184,
                'longitude_origin': 0.0,
                'longitude_step': 0.16483516991138458
            },
            {
                'latitude': -51.985939025878906,
                'point_count': 2180,
                'longitude_origin': 0.0,
                'longitude_step': 0.1651376187801361
            },
            {
                'latitude': -52.05624008178711,
                'point_count': 2176,
                'longitude_origin': 0.0,
                'longitude_step': 0.16544117033481598
            },
            {
                'latitude': -52.12653732299805,
                'point_count': 2172,
                'longitude_origin': 0.0,
                'longitude_step': 0.16574585437774658
            },
            {
                'latitude': -52.196834564208984,
                'point_count': 2168,
                'longitude_origin': 0.0,
                'longitude_step': 0.16605165600776672
            },
            {
                'latitude': -52.26713562011719,
                'point_count': 2164,
                'longitude_origin': 0.0,
                'longitude_step': 0.1663585901260376
            },
            {
                'latitude': -52.337432861328125,
                'point_count': 2160,
                'longitude_origin': 0.0,
                'longitude_step': 0.1666666716337204
            },
            {
                'latitude': -52.40773391723633,
                'point_count': 2156,
                'longitude_origin': 0.0,
                'longitude_step': 0.16697588562965393
            },
            {
                'latitude': -52.478031158447266,
                'point_count': 2152,
                'longitude_origin': 0.0,
                'longitude_step': 0.1672862470149994
            },
            {
                'latitude': -52.5483283996582,
                'point_count': 2148,
                'longitude_origin': 0.0,
                'longitude_step': 0.16759777069091797
            },
            {
                'latitude': -52.618629455566406,
                'point_count': 2144,
                'longitude_origin': 0.0,
                'longitude_step': 0.16791044175624847
            },
            {
                'latitude': -52.688926696777344,
                'point_count': 2140,
                'longitude_origin': 0.0,
                'longitude_step': 0.1682243049144745
            },
            {
                'latitude': -52.75922775268555,
                'point_count': 2136,
                'longitude_origin': 0.0,
                'longitude_step': 0.16853933036327362
            },
            {
                'latitude': -52.829524993896484,
                'point_count': 2132,
                'longitude_origin': 0.0,
                'longitude_step': 0.16885553300380707
            },
            {
                'latitude': -52.89982223510742,
                'point_count': 2128,
                'longitude_origin': 0.0,
                'longitude_step': 0.16917292773723602
            },
            {
                'latitude': -52.970123291015625,
                'point_count': 2124,
                'longitude_origin': 0.0,
                'longitude_step': 0.16949152946472168
            },
            {
                'latitude': -53.04042053222656,
                'point_count': 2120,
                'longitude_origin': 0.0,
                'longitude_step': 0.16981132328510284
            },
            {
                'latitude': -53.110721588134766,
                'point_count': 2116,
                'longitude_origin': 0.0,
                'longitude_step': 0.1701323240995407
            },
            {
                'latitude': -53.1810188293457,
                'point_count': 2112,
                'longitude_origin': 0.0,
                'longitude_step': 0.17045454680919647
            },
            {
                'latitude': -53.25131607055664,
                'point_count': 2108,
                'longitude_origin': 0.0,
                'longitude_step': 0.17077799141407013
            },
            {
                'latitude': -53.321617126464844,
                'point_count': 2104,
                'longitude_origin': 0.0,
                'longitude_step': 0.17110265791416168
            },
            {
                'latitude': -53.39191436767578,
                'point_count': 2100,
                'longitude_origin': 0.0,
                'longitude_step': 0.17142857611179352
            },
            {
                'latitude': -53.462215423583984,
                'point_count': 2096,
                'longitude_origin': 0.0,
                'longitude_step': 0.17175573110580444
            },
            {
                'latitude': -53.53251266479492,
                'point_count': 2092,
                'longitude_origin': 0.0,
                'longitude_step': 0.17208412289619446
            },
            {
                'latitude': -53.60280990600586,
                'point_count': 2088,
                'longitude_origin': 0.0,
                'longitude_step': 0.17241379618644714
            },
            {
                'latitude': -53.67311096191406,
                'point_count': 2084,
                'longitude_origin': 0.0,
                'longitude_step': 0.1727447211742401
            },
            {
                'latitude': -53.743408203125,
                'point_count': 2080,
                'longitude_origin': 0.0,
                'longitude_step': 0.17307692766189575
            },
            {
                'latitude': -53.8137092590332,
                'point_count': 2076,
                'longitude_origin': 0.0,
                'longitude_step': 0.17341040074825287
            },
            {
                'latitude': -53.88400650024414,
                'point_count': 2072,
                'longitude_origin': 0.0,
                'longitude_step': 0.17374517023563385
            },
            {
                'latitude': -53.954307556152344,
                'point_count': 2068,
                'longitude_origin': 0.0,
                'longitude_step': 0.1740812361240387
            },
            {
                'latitude': -54.02460479736328,
                'point_count': 2064,
                'longitude_origin': 0.0,
                'longitude_step': 0.1744185984134674
            },
            {
                'latitude': -54.09490203857422,
                'point_count': 2060,
                'longitude_origin': 0.0,
                'longitude_step': 0.17475728690624237
            },
            {
                'latitude': -54.16520309448242,
                'point_count': 2056,
                'longitude_origin': 0.0,
                'longitude_step': 0.1750972718000412
            },
            {
                'latitude': -54.23550033569336,
                'point_count': 2052,
                'longitude_origin': 0.0,
                'longitude_step': 0.17543859779834747
            },
            {
                'latitude': -54.30580139160156,
                'point_count': 2048,
                'longitude_origin': 0.0,
                'longitude_step': 0.17578125
            },
            {
                'latitude': -54.3760986328125,
                'point_count': 2044,
                'longitude_origin': 0.0,
                'longitude_step': 0.17612524330615997
            },
            {
                'latitude': -54.44639587402344,
                'point_count': 2040,
                'longitude_origin': 0.0,
                'longitude_step': 0.1764705926179886
            },
            {
                'latitude': -54.51669692993164,
                'point_count': 2036,
                'longitude_origin': 0.0,
                'longitude_step': 0.17681728303432465
            },
            {
                'latitude': -54.58699417114258,
                'point_count': 2032,
                'longitude_origin': 0.0,
                'longitude_step': 0.17716535925865173
            },
            {
                'latitude': -54.65729522705078,
                'point_count': 2028,
                'longitude_origin': 0.0,
                'longitude_step': 0.17751479148864746
            },
            {
                'latitude': -54.72759246826172,
                'point_count': 2024,
                'longitude_origin': 0.0,
                'longitude_step': 0.17786560952663422
            },
            {
                'latitude': -54.797889709472656,
                'point_count': 2020,
                'longitude_origin': 0.0,
                'longitude_step': 0.1782178282737732
            },
            {
                'latitude': -54.86819076538086,
                'point_count': 2016,
                'longitude_origin': 0.0,
                'longitude_step': 0.1785714328289032
            },
            {
                'latitude': -54.9384880065918,
                'point_count': 2012,
                'longitude_origin': 0.0,
                'longitude_step': 0.17892643809318542
            },
            {
                'latitude': -55.0087890625,
                'point_count': 2008,
                'longitude_origin': 0.0,
                'longitude_step': 0.17928287386894226
            },
            {
                'latitude': -55.07908630371094,
                'point_count': 2004,
                'longitude_origin': 0.0,
                'longitude_step': 0.1796407252550125
            },
            {
                'latitude': -55.149383544921875,
                'point_count': 2000,
                'longitude_origin': 0.0,
                'longitude_step': 0.18000000715255737
            },
            {
                'latitude': -55.21968460083008,
                'point_count': 1996,
                'longitude_origin': 0.0,
                'longitude_step': 0.18036071956157684
            },
            {
                'latitude': -55.289981842041016,
                'point_count': 1992,
                'longitude_origin': 0.0,
                'longitude_step': 0.1807228922843933
            },
            {
                'latitude': -55.36028289794922,
                'point_count': 1988,
                'longitude_origin': 0.0,
                'longitude_step': 0.18108652532100677
            },
            {
                'latitude': -55.430580139160156,
                'point_count': 1984,
                'longitude_origin': 0.0,
                'longitude_step': 0.18145161867141724
            },
            {
                'latitude': -55.500877380371094,
                'point_count': 1980,
                'longitude_origin': 0.0,
                'longitude_step': 0.1818181872367859
            },
            {
                'latitude': -55.5711784362793,
                'point_count': 1976,
                'longitude_origin': 0.0,
                'longitude_step': 0.18218623101711273
            },
            {
                'latitude': -55.641475677490234,
                'point_count': 1972,
                'longitude_origin': 0.0,
                'longitude_step': 0.18255577981472015
            },
            {
                'latitude': -55.71177673339844,
                'point_count': 1968,
                'longitude_origin': 0.0,
                'longitude_step': 0.18292683362960815
            },
            {
                'latitude': -55.782073974609375,
                'point_count': 1964,
                'longitude_origin': 0.0,
                'longitude_step': 0.18329939246177673
            },
            {
                'latitude': -55.85237121582031,
                'point_count': 1960,
                'longitude_origin': 0.0,
                'longitude_step': 0.18367347121238708
            },
            {
                'latitude': -55.922672271728516,
                'point_count': 1956,
                'longitude_origin': 0.0,
                'longitude_step': 0.1840490847826004
            },
            {
                'latitude': -55.99296951293945,
                'point_count': 1952,
                'longitude_origin': 0.0,
                'longitude_step': 0.1844262331724167
            },
            {
                'latitude': -56.063270568847656,
                'point_count': 1948,
                'longitude_origin': 0.0,
                'longitude_step': 0.18480493128299713
            },
            {
                'latitude': -56.133567810058594,
                'point_count': 1944,
                'longitude_origin': 0.0,
                'longitude_step': 0.18518517911434174
            },
            {
                'latitude': -56.20386505126953,
                'point_count': 1940,
                'longitude_origin': 0.0,
                'longitude_step': 0.1855670064687729
            },
            {
                'latitude': -56.274166107177734,
                'point_count': 1936,
                'longitude_origin': 0.0,
                'longitude_step': 0.1859504133462906
            },
            {
                'latitude': -56.34446334838867,
                'point_count': 1932,
                'longitude_origin': 0.0,
                'longitude_step': 0.18633539974689484
            },
            {
                'latitude': -56.414764404296875,
                'point_count': 1928,
                'longitude_origin': 0.0,
                'longitude_step': 0.18672199547290802
            },
            {
                'latitude': -56.48506164550781,
                'point_count': 1924,
                'longitude_origin': 0.0,
                'longitude_step': 0.18711018562316895
            },
            {
                'latitude': -56.55535888671875,
                'point_count': 1920,
                'longitude_origin': 0.0,
                'longitude_step': 0.1875
            },
            {
                'latitude': -56.62565994262695,
                'point_count': 1916,
                'longitude_origin': 0.0,
                'longitude_step': 0.18789143860340118
            },
            {
                'latitude': -56.69595718383789,
                'point_count': 1912,
                'longitude_origin': 0.0,
                'longitude_step': 0.1882845163345337
            },
            {
                'latitude': -56.766258239746094,
                'point_count': 1908,
                'longitude_origin': 0.0,
                'longitude_step': 0.18867924809455872
            },
            {
                'latitude': -56.83655548095703,
                'point_count': 1904,
                'longitude_origin': 0.0,
                'longitude_step': 0.18907563388347626
            },
            {
                'latitude': -56.90685272216797,
                'point_count': 1900,
                'longitude_origin': 0.0,
                'longitude_step': 0.1894736886024475
            },
            {
                'latitude': -56.97715377807617,
                'point_count': 1896,
                'longitude_origin': 0.0,
                'longitude_step': 0.18987341225147247
            },
            {
                'latitude': -57.04745101928711,
                'point_count': 1892,
                'longitude_origin': 0.0,
                'longitude_step': 0.19027483463287354
            },
            {
                'latitude': -57.11775207519531,
                'point_count': 1888,
                'longitude_origin': 0.0,
                'longitude_step': 0.1906779706478119
            },
            {
                'latitude': -57.18804931640625,
                'point_count': 1884,
                'longitude_origin': 0.0,
                'longitude_step': 0.19108280539512634
            },
            {
                'latitude': -57.25834655761719,
                'point_count': 1880,
                'longitude_origin': 0.0,
                'longitude_step': 0.19148936867713928
            },
            {
                'latitude': -57.32864761352539,
                'point_count': 1876,
                'longitude_origin': 0.0,
                'longitude_step': 0.1918976604938507
            },
            {
                'latitude': -57.39894485473633,
                'point_count': 1872,
                'longitude_origin': 0.0,
                'longitude_step': 0.19230769574642181
            },
            {
                'latitude': -57.46924591064453,
                'point_count': 1868,
                'longitude_origin': 0.0,
                'longitude_step': 0.1927194893360138
            },
            {
                'latitude': -57.53954315185547,
                'point_count': 1864,
                'longitude_origin': 0.0,
                'longitude_step': 0.19313304126262665
            },
            {
                'latitude': -57.609840393066406,
                'point_count': 1860,
                'longitude_origin': 0.0,
                'longitude_step': 0.19354838132858276
            },
            {
                'latitude': -57.68014144897461,
                'point_count': 1856,
                'longitude_origin': 0.0,
                'longitude_step': 0.19396552443504333
            },
            {
                'latitude': -57.75043869018555,
                'point_count': 1852,
                'longitude_origin': 0.0,
                'longitude_step': 0.19438445568084717
            },
            {
                'latitude': -57.82073974609375,
                'point_count': 1848,
                'longitude_origin': 0.0,
                'longitude_step': 0.19480518996715546
            },
            {
                'latitude': -57.89103698730469,
                'point_count': 1844,
                'longitude_origin': 0.0,
                'longitude_step': 0.19522777199745178
            },
            {
                'latitude': -57.961334228515625,
                'point_count': 1840,
                'longitude_origin': 0.0,
                'longitude_step': 0.19565217196941376
            },
            {
                'latitude': -58.03163528442383,
                'point_count': 1836,
                'longitude_origin': 0.0,
                'longitude_step': 0.19607843458652496
            },
            {
                'latitude': -58.101932525634766,
                'point_count': 1832,
                'longitude_origin': 0.0,
                'longitude_step': 0.1965065449476242
            },
            {
                'latitude': -58.17223358154297,
                'point_count': 1828,
                'longitude_origin': 0.0,
                'longitude_step': 0.19693654775619507
            },
            {
                'latitude': -58.242530822753906,
                'point_count': 1824,
                'longitude_origin': 0.0,
                'longitude_step': 0.19736842811107635
            },
            {
                'latitude': -58.312828063964844,
                'point_count': 1820,
                'longitude_origin': 0.0,
                'longitude_step': 0.19780220091342926
            },
            {
                'latitude': -58.38312911987305,
                'point_count': 1816,
                'longitude_origin': 0.0,
                'longitude_step': 0.19823788106441498
            },
            {
                'latitude': -58.453426361083984,
                'point_count': 1812,
                'longitude_origin': 0.0,
                'longitude_step': 0.1986754983663559
            },
            {
                'latitude': -58.52372741699219,
                'point_count': 1808,
                'longitude_origin': 0.0,
                'longitude_step': 0.19911503791809082
            },
            {
                'latitude': -58.594024658203125,
                'point_count': 1804,
                'longitude_origin': 0.0,
                'longitude_step': 0.19955654442310333
            },
            {
                'latitude': -58.66432189941406,
                'point_count': 1800,
                'longitude_origin': 0.0,
                'longitude_step': 0.20000000298023224
            },
            {
                'latitude': -58.734622955322266,
                'point_count': 1796,
                'longitude_origin': 0.0,
                'longitude_step': 0.20044542849063873
            },
            {
                'latitude': -58.8049201965332,
                'point_count': 1792,
                'longitude_origin': 0.0,
                'longitude_step': 0.2008928507566452
            },
            {
                'latitude': -58.875221252441406,
                'point_count': 1788,
                'longitude_origin': 0.0,
                'longitude_step': 0.20134228467941284
            },
            {
                'latitude': -58.945518493652344,
                'point_count': 1784,
                'longitude_origin': 0.0,
                'longitude_step': 0.20179371535778046
            },
            {
                'latitude': -59.01581573486328,
                'point_count': 1780,
                'longitude_origin': 0.0,
                'longitude_step': 0.20224718749523163
            },
            {
                'latitude': -59.086116790771484,
                'point_count': 1776,
                'longitude_origin': 0.0,
                'longitude_step': 0.20270270109176636
            },
            {
                'latitude': -59.15641403198242,
                'point_count': 1772,
                'longitude_origin': 0.0,
                'longitude_step': 0.20316027104854584
            },
            {
                'latitude': -59.226715087890625,
                'point_count': 1768,
                'longitude_origin': 0.0,
                'longitude_step': 0.20361991226673126
            },
            {
                'latitude': -59.29701232910156,
                'point_count': 1764,
                'longitude_origin': 0.0,
                'longitude_step': 0.20408163964748383
            },
            {
                'latitude': -59.3673095703125,
                'point_count': 1760,
                'longitude_origin': 0.0,
                'longitude_step': 0.20454545319080353
            },
            {
                'latitude': -59.4376106262207,
                'point_count': 1756,
                'longitude_origin': 0.0,
                'longitude_step': 0.20501138269901276
            },
            {
                'latitude': -59.50790786743164,
                'point_count': 1752,
                'longitude_origin': 0.0,
                'longitude_step': 0.2054794579744339
            },
            {
                'latitude': -59.578208923339844,
                'point_count': 1748,
                'longitude_origin': 0.0,
                'longitude_step': 0.20594966411590576
            },
            {
                'latitude': -59.64850616455078,
                'point_count': 1744,
                'longitude_origin': 0.0,
                'longitude_step': 0.20642201602458954
            },
            {
                'latitude': -59.71880340576172,
                'point_count': 1740,
                'longitude_origin': 0.0,
                'longitude_step': 0.2068965584039688
            },
            {
                'latitude': -59.78910446166992,
                'point_count': 1736,
                'longitude_origin': 0.0,
                'longitude_step': 0.20737327635288239
            },
            {
                'latitude': -59.85940170288086,
                'point_count': 1732,
                'longitude_origin': 0.0,
                'longitude_step': 0.20785219967365265
            },
            {
                'latitude': -59.92970275878906,
                'point_count': 1728,
                'longitude_origin': 0.0,
                'longitude_step': 0.2083333283662796
            },
            {
                'latitude': -60.0,
                'point_count': 1724,
                'longitude_origin': 0.0,
                'longitude_step': 0.20881670713424683
            },
            {
                'latitude': -60.07029724121094,
                'point_count': 1720,
                'longitude_origin': 0.0,
                'longitude_step': 0.20930232107639313
            },
            {
                'latitude': -60.14059829711914,
                'point_count': 1716,
                'longitude_origin': 0.0,
                'longitude_step': 0.2097902148962021
            },
            {
                'latitude': -60.21089553833008,
                'point_count': 1712,
                'longitude_origin': 0.0,
                'longitude_step': 0.2102803736925125
            },
            {
                'latitude': -60.28119659423828,
                'point_count': 1708,
                'longitude_origin': 0.0,
                'longitude_step': 0.2107728272676468
            },
            {
                'latitude': -60.35149383544922,
                'point_count': 1704,
                'longitude_origin': 0.0,
                'longitude_step': 0.2112676054239273
            },
            {
                'latitude': -60.421791076660156,
                'point_count': 1700,
                'longitude_origin': 0.0,
                'longitude_step': 0.21176470816135406
            },
            {
                'latitude': -60.49209213256836,
                'point_count': 1696,
                'longitude_origin': 0.0,
                'longitude_step': 0.21226415038108826
            },
            {
                'latitude': -60.5623893737793,
                'point_count': 1692,
                'longitude_origin': 0.0,
                'longitude_step': 0.21276596188545227
            },
            {
                'latitude': -60.6326904296875,
                'point_count': 1688,
                'longitude_origin': 0.0,
                'longitude_step': 0.2132701426744461
            },
            {
                'latitude': -60.70298767089844,
                'point_count': 1684,
                'longitude_origin': 0.0,
                'longitude_step': 0.21377672255039215
            },
            {
                'latitude': -60.773284912109375,
                'point_count': 1680,
                'longitude_origin': 0.0,
                'longitude_step': 0.2142857164144516
            },
            {
                'latitude': -60.84358596801758,
                'point_count': 1676,
                'longitude_origin': 0.0,
                'longitude_step': 0.21479713916778564
            },
            {
                'latitude': -60.913883209228516,
                'point_count': 1672,
                'longitude_origin': 0.0,
                'longitude_step': 0.21531100571155548
            },
            {
                'latitude': -60.98418426513672,
                'point_count': 1668,
                'longitude_origin': 0.0,
                'longitude_step': 0.2158273309469223
            },
            {
                'latitude': -61.054481506347656,
                'point_count': 1664,
                'longitude_origin': 0.0,
                'longitude_step': 0.2163461595773697
            },
            {
                'latitude': -61.124778747558594,
                'point_count': 1660,
                'longitude_origin': 0.0,
                'longitude_step': 0.21686747670173645
            },
            {
                'latitude': -61.1950798034668,
                'point_count': 1656,
                'longitude_origin': 0.0,
                'longitude_step': 0.21739129722118378
            },
            {
                'latitude': -61.265377044677734,
                'point_count': 1652,
                'longitude_origin': 0.0,
                'longitude_step': 0.21791768074035645
            },
            {
                'latitude': -61.33567810058594,
                'point_count': 1648,
                'longitude_origin': 0.0,
                'longitude_step': 0.21844659745693207
            },
            {
                'latitude': -61.405975341796875,
                'point_count': 1644,
                'longitude_origin': 0.0,
                'longitude_step': 0.21897810697555542
            },
            {
                'latitude': -61.47627258300781,
                'point_count': 1640,
                'longitude_origin': 0.0,
                'longitude_step': 0.2195121943950653
            },
            {
                'latitude': -61.546573638916016,
                'point_count': 1636,
                'longitude_origin': 0.0,
                'longitude_step': 0.2200489044189453
            },
            {
                'latitude': -61.61687088012695,
                'point_count': 1632,
                'longitude_origin': 0.0,
                'longitude_step': 0.22058823704719543
            },
            {
                'latitude': -61.687171936035156,
                'point_count': 1628,
                'longitude_origin': 0.0,
                'longitude_step': 0.22113022208213806
            },
            {
                'latitude': -61.757469177246094,
                'point_count': 1624,
                'longitude_origin': 0.0,
                'longitude_step': 0.2216748744249344
            },
            {
                'latitude': -61.82776641845703,
                'point_count': 1620,
                'longitude_origin': 0.0,
                'longitude_step': 0.2222222238779068
            },
            {
                'latitude': -61.898067474365234,
                'point_count': 1616,
                'longitude_origin': 0.0,
                'longitude_step': 0.2227722704410553
            },
            {
                'latitude': -61.96836471557617,
                'point_count': 1612,
                'longitude_origin': 0.0,
                'longitude_step': 0.22332505881786346
            },
            {
                'latitude': -62.038665771484375,
                'point_count': 1608,
                'longitude_origin': 0.0,
                'longitude_step': 0.2238806039094925
            },
            {
                'latitude': -62.10896301269531,
                'point_count': 1604,
                'longitude_origin': 0.0,
                'longitude_step': 0.22443890571594238
            },
            {
                'latitude': -62.17926025390625,
                'point_count': 1600,
                'longitude_origin': 0.0,
                'longitude_step': 0.22499999403953552
            },
            {
                'latitude': -62.24956130981445,
                'point_count': 1596,
                'longitude_origin': 0.0,
                'longitude_step': 0.2255639135837555
            },
            {
                'latitude': -62.31985855102539,
                'point_count': 1592,
                'longitude_origin': 0.0,
                'longitude_step': 0.2261306494474411
            },
            {
                'latitude': -62.390159606933594,
                'point_count': 1588,
                'longitude_origin': 0.0,
                'longitude_step': 0.22670024633407593
            },
            {
                'latitude': -62.46045684814453,
                'point_count': 1584,
                'longitude_origin': 0.0,
                'longitude_step': 0.22727273404598236
            },
            {
                'latitude': -62.53075408935547,
                'point_count': 1580,
                'longitude_origin': 0.0,
                'longitude_step': 0.2278480976819992
            },
            {
                'latitude': -62.60105514526367,
                'point_count': 1576,
                'longitude_origin': 0.0,
                'longitude_step': 0.22842639684677124
            },
            {
                'latitude': -62.67135238647461,
                'point_count': 1572,
                'longitude_origin': 0.0,
                'longitude_step': 0.22900763154029846
            },
            {
                'latitude': -62.74165344238281,
                'point_count': 1568,
                'longitude_origin': 0.0,
                'longitude_step': 0.22959183156490326
            },
            {
                'latitude': -62.81195068359375,
                'point_count': 1564,
                'longitude_origin': 0.0,
                'longitude_step': 0.23017902672290802
            },
            {
                'latitude': -62.88224792480469,
                'point_count': 1560,
                'longitude_origin': 0.0,
                'longitude_step': 0.23076923191547394
            },
            {
                'latitude': -62.95254898071289,
                'point_count': 1556,
                'longitude_origin': 0.0,
                'longitude_step': 0.2313624620437622
            },
            {
                'latitude': -63.02284622192383,
                'point_count': 1552,
                'longitude_origin': 0.0,
                'longitude_step': 0.2319587618112564
            },
            {
                'latitude': -63.09314727783203,
                'point_count': 1548,
                'longitude_origin': 0.0,
                'longitude_step': 0.23255814611911774
            },
            {
                'latitude': -63.16344451904297,
                'point_count': 1544,
                'longitude_origin': 0.0,
                'longitude_step': 0.2331606149673462
            },
            {
                'latitude': -63.233741760253906,
                'point_count': 1540,
                'longitude_origin': 0.0,
                'longitude_step': 0.23376622796058655
            },
            {
                'latitude': -63.30404281616211,
                'point_count': 1536,
                'longitude_origin': 0.0,
                'longitude_step': 0.234375
            },
            {
                'latitude': -63.37434005737305,
                'point_count': 1532,
                'longitude_origin': 0.0,
                'longitude_step': 0.23498694598674774
            },
            {
                'latitude': -63.44464111328125,
                'point_count': 1528,
                'longitude_origin': 0.0,
                'longitude_step': 0.23560209572315216
            },
            {
                'latitude': -63.51493835449219,
                'point_count': 1524,
                'longitude_origin': 0.0,
                'longitude_step': 0.23622047901153564
            },
            {
                'latitude': -63.585235595703125,
                'point_count': 1520,
                'longitude_origin': 0.0,
                'longitude_step': 0.2368421107530594
            },
            {
                'latitude': -63.65553665161133,
                'point_count': 1516,
                'longitude_origin': 0.0,
                'longitude_step': 0.23746702075004578
            },
            {
                'latitude': -63.725833892822266,
                'point_count': 1512,
                'longitude_origin': 0.0,
                'longitude_step': 0.2380952388048172
            },
            {
                'latitude': -63.79613494873047,
                'point_count': 1508,
                'longitude_origin': 0.0,
                'longitude_step': 0.23872679471969604
            },
            {
                'latitude': -63.866432189941406,
                'point_count': 1504,
                'longitude_origin': 0.0,
                'longitude_step': 0.2393617033958435
            },
            {
                'latitude': -63.936729431152344,
                'point_count': 1500,
                'longitude_origin': 0.0,
                'longitude_step': 0.23999999463558197
            },
            {
                'latitude': -64.00702667236328,
                'point_count': 1496,
                'longitude_origin': 0.0,
                'longitude_step': 0.24064171314239502
            },
            {
                'latitude': -64.07733154296875,
                'point_count': 1492,
                'longitude_origin': 0.0,
                'longitude_step': 0.24128685891628265
            },
            {
                'latitude': -64.14762878417969,
                'point_count': 1488,
                'longitude_origin': 0.0,
                'longitude_step': 0.24193547666072845
            },
            {
                'latitude': -64.21792602539062,
                'point_count': 1484,
                'longitude_origin': 0.0,
                'longitude_step': 0.2425875961780548
            },
            {
                'latitude': -64.28822326660156,
                'point_count': 1480,
                'longitude_origin': 0.0,
                'longitude_step': 0.2432432472705841
            },
            {
                'latitude': -64.3585205078125,
                'point_count': 1476,
                'longitude_origin': 0.0,
                'longitude_step': 0.24390244483947754
            },
            {
                'latitude': -64.42882537841797,
                'point_count': 1472,
                'longitude_origin': 0.0,
                'longitude_step': 0.2445652186870575
            },
            {
                'latitude': -64.4991226196289,
                'point_count': 1468,
                'longitude_origin': 0.0,
                'longitude_step': 0.24523161351680756
            },
            {
                'latitude': -64.56941986083984,
                'point_count': 1464,
                'longitude_origin': 0.0,
                'longitude_step': 0.24590164422988892
            },
            {
                'latitude': -64.63971710205078,
                'point_count': 1460,
                'longitude_origin': 0.0,
                'longitude_step': 0.24657534062862396
            },
            {
                'latitude': -64.71001434326172,
                'point_count': 1456,
                'longitude_origin': 0.0,
                'longitude_step': 0.24725274741649628
            },
            {
                'latitude': -64.78031921386719,
                'point_count': 1452,
                'longitude_origin': 0.0,
                'longitude_step': 0.24793387949466705
            },
            {
                'latitude': -64.85061645507812,
                'point_count': 1448,
                'longitude_origin': 0.0,
                'longitude_step': 0.24861878156661987
            },
            {
                'latitude': -64.92091369628906,
                'point_count': 1444,
                'longitude_origin': 0.0,
                'longitude_step': 0.24930748343467712
            },
            {
                'latitude': -64.9912109375,
                'point_count': 1440,
                'longitude_origin': 0.0,
                'longitude_step': 0.25
            },
            {
                'latitude': -65.06150817871094,
                'point_count': 1436,
                'longitude_origin': 0.0,
                'longitude_step': 0.2506963908672333
            },
            {
                'latitude': -65.1318130493164,
                'point_count': 1432,
                'longitude_origin': 0.0,
                'longitude_step': 0.25139665603637695
            },
            {
                'latitude': -65.20211029052734,
                'point_count': 1428,
                'longitude_origin': 0.0,
                'longitude_step': 0.2521008551120758
            },
            {
                'latitude': -65.27240753173828,
                'point_count': 1424,
                'longitude_origin': 0.0,
                'longitude_step': 0.25280898809432983
            },
            {
                'latitude': -65.34270477294922,
                'point_count': 1420,
                'longitude_origin': 0.0,
                'longitude_step': 0.2535211145877838
            },
            {
                'latitude': -65.41300201416016,
                'point_count': 1416,
                'longitude_origin': 0.0,
                'longitude_step': 0.2542372941970825
            },
            {
                'latitude': -65.48330688476562,
                'point_count': 1412,
                'longitude_origin': 0.0,
                'longitude_step': 0.25495749711990356
            },
            {
                'latitude': -65.55360412597656,
                'point_count': 1408,
                'longitude_origin': 0.0,
                'longitude_step': 0.2556818127632141
            },
            {
                'latitude': -65.6239013671875,
                'point_count': 1404,
                'longitude_origin': 0.0,
                'longitude_step': 0.25641027092933655
            },
            {
                'latitude': -65.69419860839844,
                'point_count': 1400,
                'longitude_origin': 0.0,
                'longitude_step': 0.2571428716182709
            },
            {
                'latitude': -65.76449584960938,
                'point_count': 1396,
                'longitude_origin': 0.0,
                'longitude_step': 0.2578796446323395
            },
            {
                'latitude': -65.83480072021484,
                'point_count': 1392,
                'longitude_origin': 0.0,
                'longitude_step': 0.2586206793785095
            },
            {
                'latitude': -65.90509796142578,
                'point_count': 1388,
                'longitude_origin': 0.0,
                'longitude_step': 0.2593660056591034
            },
            {
                'latitude': -65.97539520263672,
                'point_count': 1384,
                'longitude_origin': 0.0,
                'longitude_step': 0.2601155936717987
            },
            {
                'latitude': -66.04569244384766,
                'point_count': 1380,
                'longitude_origin': 0.0,
                'longitude_step': 0.260869562625885
            },
            {
                'latitude': -66.1159896850586,
                'point_count': 1376,
                'longitude_origin': 0.0,
                'longitude_step': 0.2616279125213623
            },
            {
                'latitude': -66.18629455566406,
                'point_count': 1372,
                'longitude_origin': 0.0,
                'longitude_step': 0.262390673160553
            },
            {
                'latitude': -66.256591796875,
                'point_count': 1368,
                'longitude_origin': 0.0,
                'longitude_step': 0.2631579041481018
            },
            {
                'latitude': -66.32688903808594,
                'point_count': 1364,
                'longitude_origin': 0.0,
                'longitude_step': 0.2639296054840088
            },
            {
                'latitude': -66.39718627929688,
                'point_count': 1360,
                'longitude_origin': 0.0,
                'longitude_step': 0.2647058963775635
            },
            {
                'latitude': -66.46748352050781,
                'point_count': 1356,
                'longitude_origin': 0.0,
                'longitude_step': 0.2654867172241211
            },
            {
                'latitude': -66.53778839111328,
                'point_count': 1352,
                'longitude_origin': 0.0,
                'longitude_step': 0.2662721872329712
            },
            {
                'latitude': -66.60808563232422,
                'point_count': 1348,
                'longitude_origin': 0.0,
                'longitude_step': 0.26706230640411377
            },
            {
                'latitude': -66.67838287353516,
                'point_count': 1344,
                'longitude_origin': 0.0,
                'longitude_step': 0.2678571343421936
            },
            {
                'latitude': -66.7486801147461,
                'point_count': 1340,
                'longitude_origin': 0.0,
                'longitude_step': 0.26865673065185547
            },
            {
                'latitude': -66.81897735595703,
                'point_count': 1336,
                'longitude_origin': 0.0,
                'longitude_step': 0.269461065530777
            },
            {
                'latitude': -66.8892822265625,
                'point_count': 1332,
                'longitude_origin': 0.0,
                'longitude_step': 0.2702702581882477
            },
            {
                'latitude': -66.95957946777344,
                'point_count': 1328,
                'longitude_origin': 0.0,
                'longitude_step': 0.27108433842658997
            },
            {
                'latitude': -67.02987670898438,
                'point_count': 1324,
                'longitude_origin': 0.0,
                'longitude_step': 0.2719033360481262
            },
            {
                'latitude': -67.10017395019531,
                'point_count': 1320,
                'longitude_origin': 0.0,
                'longitude_step': 0.27272728085517883
            },
            {
                'latitude': -67.17047119140625,
                'point_count': 1316,
                'longitude_origin': 0.0,
                'longitude_step': 0.2735562324523926
            },
            {
                'latitude': -67.24077606201172,
                'point_count': 1312,
                'longitude_origin': 0.0,
                'longitude_step': 0.27439025044441223
            },
            {
                'latitude': -67.31107330322266,
                'point_count': 1308,
                'longitude_origin': 0.0,
                'longitude_step': 0.2752293646335602
            },
            {
                'latitude': -67.3813705444336,
                'point_count': 1304,
                'longitude_origin': 0.0,
                'longitude_step': 0.2760736048221588
            },
            {
                'latitude': -67.45166778564453,
                'point_count': 1300,
                'longitude_origin': 0.0,
                'longitude_step': 0.2769230902194977
            },
            {
                'latitude': -67.52196502685547,
                'point_count': 1296,
                'longitude_origin': 0.0,
                'longitude_step': 0.2777777910232544
            },
            {
                'latitude': -67.59226989746094,
                'point_count': 1292,
                'longitude_origin': 0.0,
                'longitude_step': 0.27863776683807373
            },
            {
                'latitude': -67.66256713867188,
                'point_count': 1288,
                'longitude_origin': 0.0,
                'longitude_step': 0.27950310707092285
            },
            {
                'latitude': -67.73286437988281,
                'point_count': 1284,
                'longitude_origin': 0.0,
                'longitude_step': 0.28037384152412415
            },
            {
                'latitude': -67.80316162109375,
                'point_count': 1280,
                'longitude_origin': 0.0,
                'longitude_step': 0.28125
            },
            {
                'latitude': -67.87345886230469,
                'point_count': 1276,
                'longitude_origin': 0.0,
                'longitude_step': 0.2821316719055176
            },
            {
                'latitude': -67.94376373291016,
                'point_count': 1272,
                'longitude_origin': 0.0,
                'longitude_step': 0.2830188572406769
            },
            {
                'latitude': -68.0140609741211,
                'point_count': 1268,
                'longitude_origin': 0.0,
                'longitude_step': 0.28391167521476746
            },
            {
                'latitude': -68.08435821533203,
                'point_count': 1264,
                'longitude_origin': 0.0,
                'longitude_step': 0.2848101258277893
            },
            {
                'latitude': -68.15465545654297,
                'point_count': 1260,
                'longitude_origin': 0.0,
                'longitude_step': 0.2857142984867096
            },
            {
                'latitude': -68.2249526977539,
                'point_count': 1256,
                'longitude_origin': 0.0,
                'longitude_step': 0.2866241931915283
            },
            {
                'latitude': -68.29525756835938,
                'point_count': 1252,
                'longitude_origin': 0.0,
                'longitude_step': 0.28753992915153503
            },
            {
                'latitude': -68.36555480957031,
                'point_count': 1248,
                'longitude_origin': 0.0,
                'longitude_step': 0.2884615361690521
            },
            {
                'latitude': -68.43585205078125,
                'point_count': 1244,
                'longitude_origin': 0.0,
                'longitude_step': 0.28938907384872437
            },
            {
                'latitude': -68.50614929199219,
                'point_count': 1240,
                'longitude_origin': 0.0,
                'longitude_step': 0.29032257199287415
            },
            {
                'latitude': -68.57644653320312,
                'point_count': 1236,
                'longitude_origin': 0.0,
                'longitude_step': 0.291262149810791
            },
            {
                'latitude': -68.6467514038086,
                'point_count': 1232,
                'longitude_origin': 0.0,
                'longitude_step': 0.2922077775001526
            },
            {
                'latitude': -68.71704864501953,
                'point_count': 1228,
                'longitude_origin': 0.0,
                'longitude_step': 0.2931596040725708
            },
            {
                'latitude': -68.78734588623047,
                'point_count': 1224,
                'longitude_origin': 0.0,
                'longitude_step': 0.29411765933036804
            },
            {
                'latitude': -68.8576431274414,
                'point_count': 1220,
                'longitude_origin': 0.0,
                'longitude_step': 0.2950819730758667
            },
            {
                'latitude': -68.92794036865234,
                'point_count': 1216,
                'longitude_origin': 0.0,
                'longitude_step': 0.29605263471603394
            },
            {
                'latitude': -68.99824523925781,
                'point_count': 1212,
                'longitude_origin': 0.0,
                'longitude_step': 0.2970297038555145
            },
            {
                'latitude': -69.06854248046875,
                'point_count': 1208,
                'longitude_origin': 0.0,
                'longitude_step': 0.29801324009895325
            },
            {
                'latitude': -69.13883972167969,
                'point_count': 1204,
                'longitude_origin': 0.0,
                'longitude_step': 0.29900333285331726
            },
            {
                'latitude': -69.20913696289062,
                'point_count': 1200,
                'longitude_origin': 0.0,
                'longitude_step': 0.30000001192092896
            },
            {
                'latitude': -69.27943420410156,
                'point_count': 1196,
                'longitude_origin': 0.0,
                'longitude_step': 0.3010033369064331
            },
            {
                'latitude': -69.34973907470703,
                'point_count': 1192,
                'longitude_origin': 0.0,
                'longitude_step': 0.30201342701911926
            },
            {
                'latitude': -69.42003631591797,
                'point_count': 1188,
                'longitude_origin': 0.0,
                'longitude_step': 0.3030303120613098
            },
            {
                'latitude': -69.4903335571289,
                'point_count': 1184,
                'longitude_origin': 0.0,
                'longitude_step': 0.30405405163764954
            },
            {
                'latitude': -69.56063079833984,
                'point_count': 1180,
                'longitude_origin': 0.0,
                'longitude_step': 0.3050847351551056
            },
            {
                'latitude': -69.63092803955078,
                'point_count': 1176,
                'longitude_origin': 0.0,
                'longitude_step': 0.30612245202064514
            },
            {
                'latitude': -69.70123291015625,
                'point_count': 1172,
                'longitude_origin': 0.0,
                'longitude_step': 0.3071672320365906
            },
            {
                'latitude': -69.77153015136719,
                'point_count': 1168,
                'longitude_origin': 0.0,
                'longitude_step': 0.30821916460990906
            },
            {
                'latitude': -69.84182739257812,
                'point_count': 1164,
                'longitude_origin': 0.0,
                'longitude_step': 0.30927833914756775
            },
            {
                'latitude': -69.91212463378906,
                'point_count': 1160,
                'longitude_origin': 0.0,
                'longitude_step': 0.3103448152542114
            },
            {
                'latitude': -69.982421875,
                'point_count': 1156,
                'longitude_origin': 0.0,
                'longitude_step': 0.31141868233680725
            },
            {
                'latitude': -70.05272674560547,
                'point_count': 1152,
                'longitude_origin': 0.0,
                'longitude_step': 0.3125
            },
            {
                'latitude': -70.1230239868164,
                'point_count': 1148,
                'longitude_origin': 0.0,
                'longitude_step': 0.31358885765075684
            },
            {
                'latitude': -70.19332122802734,
                'point_count': 1144,
                'longitude_origin': 0.0,
                'longitude_step': 0.31468531489372253
            },
            {
                'latitude': -70.26361846923828,
                'point_count': 1140,
                'longitude_origin': 0.0,
                'longitude_step': 0.31578946113586426
            },
            {
                'latitude': -70.33391571044922,
                'point_count': 1136,
                'longitude_origin': 0.0,
                'longitude_step': 0.31690141558647156
            },
            {
                'latitude': -70.40422058105469,
                'point_count': 1132,
                'longitude_origin': 0.0,
                'longitude_step': 0.3180212080478668
            },
            {
                'latitude': -70.47451782226562,
                'point_count': 1128,
                'longitude_origin': 0.0,
                'longitude_step': 0.3191489279270172
            },
            {
                'latitude': -70.54481506347656,
                'point_count': 1124,
                'longitude_origin': 0.0,
                'longitude_step': 0.3202846944332123
            },
            {
                'latitude': -70.6151123046875,
                'point_count': 1120,
                'longitude_origin': 0.0,
                'longitude_step': 0.3214285671710968
            },
            {
                'latitude': -70.68540954589844,
                'point_count': 1116,
                'longitude_origin': 0.0,
                'longitude_step': 0.32258063554763794
            },
            {
                'latitude': -70.7557144165039,
                'point_count': 1112,
                'longitude_origin': 0.0,
                'longitude_step': 0.32374101877212524
            },
            {
                'latitude': -70.82601165771484,
                'point_count': 1108,
                'longitude_origin': 0.0,
                'longitude_step': 0.3249097466468811
            },
            {
                'latitude': -70.89630889892578,
                'point_count': 1104,
                'longitude_origin': 0.0,
                'longitude_step': 0.32608696818351746
            },
            {
                'latitude': -70.96660614013672,
                'point_count': 1100,
                'longitude_origin': 0.0,
                'longitude_step': 0.3272727131843567
            },
            {
                'latitude': -71.03690338134766,
                'point_count': 1096,
                'longitude_origin': 0.0,
                'longitude_step': 0.32846716046333313
            },
            {
                'latitude': -71.10720825195312,
                'point_count': 1092,
                'longitude_origin': 0.0,
                'longitude_step': 0.32967033982276917
            },
            {
                'latitude': -71.17750549316406,
                'point_count': 1088,
                'longitude_origin': 0.0,
                'longitude_step': 0.33088234066963196
            },
            {
                'latitude': -71.247802734375,
                'point_count': 1084,
                'longitude_origin': 0.0,
                'longitude_step': 0.33210331201553345
            },
            {
                'latitude': -71.31809997558594,
                'point_count': 1080,
                'longitude_origin': 0.0,
                'longitude_step': 0.3333333432674408
            },
            {
                'latitude': -71.38839721679688,
                'point_count': 1076,
                'longitude_origin': 0.0,
                'longitude_step': 0.3345724940299988
            },
            {
                'latitude': -71.45870208740234,
                'point_count': 1072,
                'longitude_origin': 0.0,
                'longitude_step': 0.33582088351249695
            },
            {
                'latitude': -71.52899932861328,
                'point_count': 1068,
                'longitude_origin': 0.0,
                'longitude_step': 0.33707866072654724
            },
            {
                'latitude': -71.59929656982422,
                'point_count': 1064,
                'longitude_origin': 0.0,
                'longitude_step': 0.33834585547447205
            },
            {
                'latitude': -71.66959381103516,
                'point_count': 1060,
                'longitude_origin': 0.0,
                'longitude_step': 0.3396226465702057
            },
            {
                'latitude': -71.7398910522461,
                'point_count': 1056,
                'longitude_origin': 0.0,
                'longitude_step': 0.34090909361839294
            },
            {
                'latitude': -71.81019592285156,
                'point_count': 1052,
                'longitude_origin': 0.0,
                'longitude_step': 0.34220531582832336
            },
            {
                'latitude': -71.8804931640625,
                'point_count': 1048,
                'longitude_origin': 0.0,
                'longitude_step': 0.3435114622116089
            },
            {
                'latitude': -71.95079040527344,
                'point_count': 1044,
                'longitude_origin': 0.0,
                'longitude_step': 0.3448275923728943
            },
            {
                'latitude': -72.02108764648438,
                'point_count': 1040,
                'longitude_origin': 0.0,
                'longitude_step': 0.3461538553237915
            },
            {
                'latitude': -72.09138488769531,
                'point_count': 1036,
                'longitude_origin': 0.0,
                'longitude_step': 0.3474903404712677
            },
            {
                'latitude': -72.16168975830078,
                'point_count': 1032,
                'longitude_origin': 0.0,
                'longitude_step': 0.3488371968269348
            },
            {
                'latitude': -72.23198699951172,
                'point_count': 1028,
                'longitude_origin': 0.0,
                'longitude_step': 0.3501945436000824
            },
            {
                'latitude': -72.30228424072266,
                'point_count': 1024,
                'longitude_origin': 0.0,
                'longitude_step': 0.3515625
            },
            {
                'latitude': -72.3725814819336,
                'point_count': 1020,
                'longitude_origin': 0.0,
                'longitude_step': 0.3529411852359772
            },
            {
                'latitude': -72.44287872314453,
                'point_count': 1016,
                'longitude_origin': 0.0,
                'longitude_step': 0.35433071851730347
            },
            {
                'latitude': -72.51318359375,
                'point_count': 1012,
                'longitude_origin': 0.0,
                'longitude_step': 0.35573121905326843
            },
            {
                'latitude': -72.58348083496094,
                'point_count': 1008,
                'longitude_origin': 0.0,
                'longitude_step': 0.3571428656578064
            },
            {
                'latitude': -72.65377807617188,
                'point_count': 1004,
                'longitude_origin': 0.0,
                'longitude_step': 0.3585657477378845
            },
            {
                'latitude': -72.72407531738281,
                'point_count': 1000,
                'longitude_origin': 0.0,
                'longitude_step': 0.36000001430511475
            },
            {
                'latitude': -72.79437255859375,
                'point_count': 996,
                'longitude_origin': 0.0,
                'longitude_step': 0.3614457845687866
            },
            {
                'latitude': -72.86467742919922,
                'point_count': 992,
                'longitude_origin': 0.0,
                'longitude_step': 0.3629032373428345
            },
            {
                'latitude': -72.93497467041016,
                'point_count': 988,
                'longitude_origin': 0.0,
                'longitude_step': 0.36437246203422546
            },
            {
                'latitude': -73.0052719116211,
                'point_count': 984,
                'longitude_origin': 0.0,
                'longitude_step': 0.3658536672592163
            },
            {
                'latitude': -73.07556915283203,
                'point_count': 980,
                'longitude_origin': 0.0,
                'longitude_step': 0.36734694242477417
            },
            {
                'latitude': -73.14586639404297,
                'point_count': 976,
                'longitude_origin': 0.0,
                'longitude_step': 0.3688524663448334
            },
            {
                'latitude': -73.21617126464844,
                'point_count': 972,
                'longitude_origin': 0.0,
                'longitude_step': 0.37037035822868347
            },
            {
                'latitude': -73.28646850585938,
                'point_count': 968,
                'longitude_origin': 0.0,
                'longitude_step': 0.3719008266925812
            },
            {
                'latitude': -73.35676574707031,
                'point_count': 964,
                'longitude_origin': 0.0,
                'longitude_step': 0.37344399094581604
            },
            {
                'latitude': -73.42706298828125,
                'point_count': 960,
                'longitude_origin': 0.0,
                'longitude_step': 0.375
            },
            {
                'latitude': -73.49736022949219,
                'point_count': 956,
                'longitude_origin': 0.0,
                'longitude_step': 0.3765690326690674
            },
            {
                'latitude': -73.56766510009766,
                'point_count': 952,
                'longitude_origin': 0.0,
                'longitude_step': 0.3781512677669525
            },
            {
                'latitude': -73.6379623413086,
                'point_count': 948,
                'longitude_origin': 0.0,
                'longitude_step': 0.37974682450294495
            },
            {
                'latitude': -73.70825958251953,
                'point_count': 944,
                'longitude_origin': 0.0,
                'longitude_step': 0.3813559412956238
            },
            {
                'latitude': -73.77855682373047,
                'point_count': 940,
                'longitude_origin': 0.0,
                'longitude_step': 0.38297873735427856
            },
            {
                'latitude': -73.8488540649414,
                'point_count': 936,
                'longitude_origin': 0.0,
                'longitude_step': 0.38461539149284363
            },
            {
                'latitude': -73.91915893554688,
                'point_count': 932,
                'longitude_origin': 0.0,
                'longitude_step': 0.3862660825252533
            },
            {
                'latitude': -73.98945617675781,
                'point_count': 928,
                'longitude_origin': 0.0,
                'longitude_step': 0.38793104887008667
            },
            {
                'latitude': -74.05975341796875,
                'point_count': 924,
                'longitude_origin': 0.0,
                'longitude_step': 0.3896103799343109
            },
            {
                'latitude': -74.13005065917969,
                'point_count': 920,
                'longitude_origin': 0.0,
                'longitude_step': 0.3913043439388275
            },
            {
                'latitude': -74.20034790039062,
                'point_count': 916,
                'longitude_origin': 0.0,
                'longitude_step': 0.3930130898952484
            },
            {
                'latitude': -74.2706527709961,
                'point_count': 912,
                'longitude_origin': 0.0,
                'longitude_step': 0.3947368562221527
            },
            {
                'latitude': -74.34095001220703,
                'point_count': 908,
                'longitude_origin': 0.0,
                'longitude_step': 0.39647576212882996
            },
            {
                'latitude': -74.41124725341797,
                'point_count': 904,
                'longitude_origin': 0.0,
                'longitude_step': 0.39823007583618164
            },
            {
                'latitude': -74.4815444946289,
                'point_count': 900,
                'longitude_origin': 0.0,
                'longitude_step': 0.4000000059604645
            },
            {
                'latitude': -74.55184173583984,
                'point_count': 896,
                'longitude_origin': 0.0,
                'longitude_step': 0.4017857015132904
            },
            {
                'latitude': -74.62214660644531,
                'point_count': 892,
                'longitude_origin': 0.0,
                'longitude_step': 0.4035874307155609
            },
            {
                'latitude': -74.69244384765625,
                'point_count': 888,
                'longitude_origin': 0.0,
                'longitude_step': 0.4054054021835327
            },
            {
                'latitude': -74.76274108886719,
                'point_count': 884,
                'longitude_origin': 0.0,
                'longitude_step': 0.4072398245334625
            },
            {
                'latitude': -74.83303833007812,
                'point_count': 880,
                'longitude_origin': 0.0,
                'longitude_step': 0.40909090638160706
            },
            {
                'latitude': -74.90333557128906,
                'point_count': 876,
                'longitude_origin': 0.0,
                'longitude_step': 0.4109589159488678
            },
            {
                'latitude': -74.97364044189453,
                'point_count': 872,
                'longitude_origin': 0.0,
                'longitude_step': 0.4128440320491791
            },
            {
                'latitude': -75.04393768310547,
                'point_count': 868,
                'longitude_origin': 0.0,
                'longitude_step': 0.41474655270576477
            },
            {
                'latitude': -75.1142349243164,
                'point_count': 864,
                'longitude_origin': 0.0,
                'longitude_step': 0.4166666567325592
            },
            {
                'latitude': -75.18453216552734,
                'point_count': 860,
                'longitude_origin': 0.0,
                'longitude_step': 0.41860464215278625
            },
            {
                'latitude': -75.25482940673828,
                'point_count': 856,
                'longitude_origin': 0.0,
                'longitude_step': 0.420560747385025
            },
            {
                'latitude': -75.32513427734375,
                'point_count': 852,
                'longitude_origin': 0.0,
                'longitude_step': 0.4225352108478546
            },
            {
                'latitude': -75.39543151855469,
                'point_count': 848,
                'longitude_origin': 0.0,
                'longitude_step': 0.4245283007621765
            },
            {
                'latitude': -75.46572875976562,
                'point_count': 844,
                'longitude_origin': 0.0,
                'longitude_step': 0.4265402853488922
            },
            {
                'latitude': -75.53602600097656,
                'point_count': 840,
                'longitude_origin': 0.0,
                'longitude_step': 0.4285714328289032
            },
            {
                'latitude': -75.6063232421875,
                'point_count': 836,
                'longitude_origin': 0.0,
                'longitude_step': 0.43062201142311096
            },
            {
                'latitude': -75.67662811279297,
                'point_count': 832,
                'longitude_origin': 0.0,
                'longitude_step': 0.4326923191547394
            },
            {
                'latitude': -75.7469253540039,
                'point_count': 828,
                'longitude_origin': 0.0,
                'longitude_step': 0.43478259444236755
            },
            {
                'latitude': -75.81722259521484,
                'point_count': 824,
                'longitude_origin': 0.0,
                'longitude_step': 0.43689319491386414
            },
            {
                'latitude': -75.88751983642578,
                'point_count': 820,
                'longitude_origin': 0.0,
                'longitude_step': 0.4390243887901306
            },
            {
                'latitude': -75.95781707763672,
                'point_count': 816,
                'longitude_origin': 0.0,
                'longitude_step': 0.44117647409439087
            },
            {
                'latitude': -76.02812194824219,
                'point_count': 812,
                'longitude_origin': 0.0,
                'longitude_step': 0.4433497488498688
            },
            {
                'latitude': -76.09841918945312,
                'point_count': 808,
                'longitude_origin': 0.0,
                'longitude_step': 0.4455445408821106
            },
            {
                'latitude': -76.16871643066406,
                'point_count': 804,
                'longitude_origin': 0.0,
                'longitude_step': 0.447761207818985
            },
            {
                'latitude': -76.239013671875,
                'point_count': 800,
                'longitude_origin': 0.0,
                'longitude_step': 0.44999998807907104
            },
            {
                'latitude': -76.30931091308594,
                'point_count': 796,
                'longitude_origin': 0.0,
                'longitude_step': 0.4522612988948822
            },
            {
                'latitude': -76.3796157836914,
                'point_count': 792,
                'longitude_origin': 0.0,
                'longitude_step': 0.4545454680919647
            },
            {
                'latitude': -76.44991302490234,
                'point_count': 788,
                'longitude_origin': 0.0,
                'longitude_step': 0.4568527936935425
            },
            {
                'latitude': -76.52021026611328,
                'point_count': 784,
                'longitude_origin': 0.0,
                'longitude_step': 0.4591836631298065
            },
            {
                'latitude': -76.59050750732422,
                'point_count': 780,
                'longitude_origin': 0.0,
                'longitude_step': 0.4615384638309479
            },
            {
                'latitude': -76.66080474853516,
                'point_count': 776,
                'longitude_origin': 0.0,
                'longitude_step': 0.4639175236225128
            },
            {
                'latitude': -76.73110961914062,
                'point_count': 772,
                'longitude_origin': 0.0,
                'longitude_step': 0.4663212299346924
            },
            {
                'latitude': -76.80140686035156,
                'point_count': 768,
                'longitude_origin': 0.0,
                'longitude_step': 0.46875
            },
            {
                'latitude': -76.8717041015625,
                'point_count': 764,
                'longitude_origin': 0.0,
                'longitude_step': 0.4712041914463043
            },
            {
                'latitude': -76.94200134277344,
                'point_count': 760,
                'longitude_origin': 0.0,
                'longitude_step': 0.4736842215061188
            },
            {
                'latitude': -77.01229858398438,
                'point_count': 756,
                'longitude_origin': 0.0,
                'longitude_step': 0.4761904776096344
            },
            {
                'latitude': -77.08260345458984,
                'point_count': 752,
                'longitude_origin': 0.0,
                'longitude_step': 0.478723406791687
            },
            {
                'latitude': -77.15290069580078,
                'point_count': 748,
                'longitude_origin': 0.0,
                'longitude_step': 0.48128342628479004
            },
            {
                'latitude': -77.22319793701172,
                'point_count': 744,
                'longitude_origin': 0.0,
                'longitude_step': 0.4838709533214569
            },
            {
                'latitude': -77.29349517822266,
                'point_count': 740,
                'longitude_origin': 0.0,
                'longitude_step': 0.4864864945411682
            },
            {
                'latitude': -77.3637924194336,
                'point_count': 736,
                'longitude_origin': 0.0,
                'longitude_step': 0.489130437374115
            },
            {
                'latitude': -77.43409729003906,
                'point_count': 732,
                'longitude_origin': 0.0,
                'longitude_step': 0.49180328845977783
            },
            {
                'latitude': -77.50439453125,
                'point_count': 728,
                'longitude_origin': 0.0,
                'longitude_step': 0.49450549483299255
            },
            {
                'latitude': -77.57469177246094,
                'point_count': 724,
                'longitude_origin': 0.0,
                'longitude_step': 0.49723756313323975
            },
            {
                'latitude': -77.64498901367188,
                'point_count': 720,
                'longitude_origin': 0.0,
                'longitude_step': 0.5
            },
            {
                'latitude': -77.71528625488281,
                'point_count': 716,
                'longitude_origin': 0.0,
                'longitude_step': 0.5027933120727539
            },
            {
                'latitude': -77.78559112548828,
                'point_count': 712,
                'longitude_origin': 0.0,
                'longitude_step': 0.5056179761886597
            },
            {
                'latitude': -77.85588836669922,
                'point_count': 708,
                'longitude_origin': 0.0,
                'longitude_step': 0.508474588394165
            },
            {
                'latitude': -77.92618560791016,
                'point_count': 704,
                'longitude_origin': 0.0,
                'longitude_step': 0.5113636255264282
            },
            {
                'latitude': -77.9964828491211,
                'point_count': 700,
                'longitude_origin': 0.0,
                'longitude_step': 0.5142857432365417
            },
            {
                'latitude': -78.06678009033203,
                'point_count': 696,
                'longitude_origin': 0.0,
                'longitude_step': 0.517241358757019
            },
            {
                'latitude': -78.1370849609375,
                'point_count': 692,
                'longitude_origin': 0.0,
                'longitude_step': 0.5202311873435974
            },
            {
                'latitude': -78.20738220214844,
                'point_count': 688,
                'longitude_origin': 0.0,
                'longitude_step': 0.5232558250427246
            },
            {
                'latitude': -78.27767944335938,
                'point_count': 684,
                'longitude_origin': 0.0,
                'longitude_step': 0.5263158082962036
            },
            {
                'latitude': -78.34797668457031,
                'point_count': 680,
                'longitude_origin': 0.0,
                'longitude_step': 0.529411792755127
            },
            {
                'latitude': -78.41827392578125,
                'point_count': 676,
                'longitude_origin': 0.0,
                'longitude_step': 0.5325443744659424
            },
            {
                'latitude': -78.48857879638672,
                'point_count': 672,
                'longitude_origin': 0.0,
                'longitude_step': 0.5357142686843872
            },
            {
                'latitude': -78.55887603759766,
                'point_count': 668,
                'longitude_origin': 0.0,
                'longitude_step': 0.538922131061554
            },
            {
                'latitude': -78.6291732788086,
                'point_count': 664,
                'longitude_origin': 0.0,
                'longitude_step': 0.5421686768531799
            },
            {
                'latitude': -78.69947052001953,
                'point_count': 660,
                'longitude_origin': 0.0,
                'longitude_step': 0.5454545617103577
            },
            {
                'latitude': -78.76976776123047,
                'point_count': 656,
                'longitude_origin': 0.0,
                'longitude_step': 0.5487805008888245
            },
            {
                'latitude': -78.84007263183594,
                'point_count': 652,
                'longitude_origin': 0.0,
                'longitude_step': 0.5521472096443176
            },
            {
                'latitude': -78.91036987304688,
                'point_count': 648,
                'longitude_origin': 0.0,
                'longitude_step': 0.5555555820465088
            },
            {
                'latitude': -78.98066711425781,
                'point_count': 644,
                'longitude_origin': 0.0,
                'longitude_step': 0.5590062141418457
            },
            {
                'latitude': -79.05096435546875,
                'point_count': 640,
                'longitude_origin': 0.0,
                'longitude_step': 0.5625
            },
            {
                'latitude': -79.12126159667969,
                'point_count': 636,
                'longitude_origin': 0.0,
                'longitude_step': 0.5660377144813538
            },
            {
                'latitude': -79.19156646728516,
                'point_count': 632,
                'longitude_origin': 0.0,
                'longitude_step': 0.5696202516555786
            },
            {
                'latitude': -79.2618637084961,
                'point_count': 628,
                'longitude_origin': 0.0,
                'longitude_step': 0.5732483863830566
            },
            {
                'latitude': -79.33216094970703,
                'point_count': 624,
                'longitude_origin': 0.0,
                'longitude_step': 0.5769230723381042
            },
            {
                'latitude': -79.40245819091797,
                'point_count': 620,
                'longitude_origin': 0.0,
                'longitude_step': 0.5806451439857483
            },
            {
                'latitude': -79.4727554321289,
                'point_count': 616,
                'longitude_origin': 0.0,
                'longitude_step': 0.5844155550003052
            },
            {
                'latitude': -79.54306030273438,
                'point_count': 612,
                'longitude_origin': 0.0,
                'longitude_step': 0.5882353186607361
            },
            {
                'latitude': -79.61335754394531,
                'point_count': 608,
                'longitude_origin': 0.0,
                'longitude_step': 0.5921052694320679
            },
            {
                'latitude': -79.68365478515625,
                'point_count': 604,
                'longitude_origin': 0.0,
                'longitude_step': 0.5960264801979065
            },
            {
                'latitude': -79.75395202636719,
                'point_count': 600,
                'longitude_origin': 0.0,
                'longitude_step': 0.6000000238418579
            },
            {
                'latitude': -79.82424926757812,
                'point_count': 596,
                'longitude_origin': 0.0,
                'longitude_step': 0.6040268540382385
            },
            {
                'latitude': -79.8945541381836,
                'point_count': 592,
                'longitude_origin': 0.0,
                'longitude_step': 0.6081081032752991
            },
            {
                'latitude': -79.96485137939453,
                'point_count': 588,
                'longitude_origin': 0.0,
                'longitude_step': 0.6122449040412903
            },
            {
                'latitude': -80.03514862060547,
                'point_count': 584,
                'longitude_origin': 0.0,
                'longitude_step': 0.6164383292198181
            },
            {
                'latitude': -80.1054458618164,
                'point_count': 580,
                'longitude_origin': 0.0,
                'longitude_step': 0.6206896305084229
            },
            {
                'latitude': -80.17574310302734,
                'point_count': 576,
                'longitude_origin': 0.0,
                'longitude_step': 0.625
            },
            {
                'latitude': -80.24604797363281,
                'point_count': 572,
                'longitude_origin': 0.0,
                'longitude_step': 0.6293706297874451
            },
            {
                'latitude': -80.31634521484375,
                'point_count': 568,
                'longitude_origin': 0.0,
                'longitude_step': 0.6338028311729431
            },
            {
                'latitude': -80.38664245605469,
                'point_count': 564,
                'longitude_origin': 0.0,
                'longitude_step': 0.6382978558540344
            },
            {
                'latitude': -80.45693969726562,
                'point_count': 560,
                'longitude_origin': 0.0,
                'longitude_step': 0.6428571343421936
            },
            {
                'latitude': -80.52723693847656,
                'point_count': 556,
                'longitude_origin': 0.0,
                'longitude_step': 0.6474820375442505
            },
            {
                'latitude': -80.59754180908203,
                'point_count': 552,
                'longitude_origin': 0.0,
                'longitude_step': 0.6521739363670349
            },
            {
                'latitude': -80.66783905029297,
                'point_count': 548,
                'longitude_origin': 0.0,
                'longitude_step': 0.6569343209266663
            },
            {
                'latitude': -80.7381362915039,
                'point_count': 544,
                'longitude_origin': 0.0,
                'longitude_step': 0.6617646813392639
            },
            {
                'latitude': -80.80843353271484,
                'point_count': 540,
                'longitude_origin': 0.0,
                'longitude_step': 0.6666666865348816
            },
            {
                'latitude': -80.87873077392578,
                'point_count': 536,
                'longitude_origin': 0.0,
                'longitude_step': 0.6716417670249939
            },
            {
                'latitude': -80.94903564453125,
                'point_count': 532,
                'longitude_origin': 0.0,
                'longitude_step': 0.6766917109489441
            },
            {
                'latitude': -81.01933288574219,
                'point_count': 528,
                'longitude_origin': 0.0,
                'longitude_step': 0.6818181872367859
            },
            {
                'latitude': -81.08963012695312,
                'point_count': 524,
                'longitude_origin': 0.0,
                'longitude_step': 0.6870229244232178
            },
            {
                'latitude': -81.15992736816406,
                'point_count': 520,
                'longitude_origin': 0.0,
                'longitude_step': 0.692307710647583
            },
            {
                'latitude': -81.230224609375,
                'point_count': 516,
                'longitude_origin': 0.0,
                'longitude_step': 0.6976743936538696
            },
            {
                'latitude': -81.30052947998047,
                'point_count': 512,
                'longitude_origin': 0.0,
                'longitude_step': 0.703125
            },
            {
                'latitude': -81.3708267211914,
                'point_count': 508,
                'longitude_origin': 0.0,
                'longitude_step': 0.7086614370346069
            },
            {
                'latitude': -81.44112396240234,
                'point_count': 504,
                'longitude_origin': 0.0,
                'longitude_step': 0.7142857313156128
            },
            {
                'latitude': -81.51142120361328,
                'point_count': 500,
                'longitude_origin': 0.0,
                'longitude_step': 0.7200000286102295
            },
            {
                'latitude': -81.58171844482422,
                'point_count': 496,
                'longitude_origin': 0.0,
                'longitude_step': 0.725806474685669
            },
            {
                'latitude': -81.65202331542969,
                'point_count': 492,
                'longitude_origin': 0.0,
                'longitude_step': 0.7317073345184326
            },
            {
                'latitude': -81.72232055664062,
                'point_count': 488,
                'longitude_origin': 0.0,
                'longitude_step': 0.7377049326896667
            },
            {
                'latitude': -81.79261779785156,
                'point_count': 484,
                'longitude_origin': 0.0,
                'longitude_step': 0.7438016533851624
            },
            {
                'latitude': -81.8629150390625,
                'point_count': 480,
                'longitude_origin': 0.0,
                'longitude_step': 0.75
            },
            {
                'latitude': -81.93321228027344,
                'point_count': 476,
                'longitude_origin': 0.0,
                'longitude_step': 0.756302535533905
            },
            {
                'latitude': -82.0035171508789,
                'point_count': 472,
                'longitude_origin': 0.0,
                'longitude_step': 0.7627118825912476
            },
            {
                'latitude': -82.07381439208984,
                'point_count': 468,
                'longitude_origin': 0.0,
                'longitude_step': 0.7692307829856873
            },
            {
                'latitude': -82.14411163330078,
                'point_count': 464,
                'longitude_origin': 0.0,
                'longitude_step': 0.7758620977401733
            },
            {
                'latitude': -82.21440887451172,
                'point_count': 460,
                'longitude_origin': 0.0,
                'longitude_step': 0.782608687877655
            },
            {
                'latitude': -82.28470611572266,
                'point_count': 456,
                'longitude_origin': 0.0,
                'longitude_step': 0.7894737124443054
            },
            {
                'latitude': -82.35501098632812,
                'point_count': 452,
                'longitude_origin': 0.0,
                'longitude_step': 0.7964601516723633
            },
            {
                'latitude': -82.42530822753906,
                'point_count': 448,
                'longitude_origin': 0.0,
                'longitude_step': 0.8035714030265808
            },
            {
                'latitude': -82.49560546875,
                'point_count': 444,
                'longitude_origin': 0.0,
                'longitude_step': 0.8108108043670654
            },
            {
                'latitude': -82.56590270996094,
                'point_count': 440,
                'longitude_origin': 0.0,
                'longitude_step': 0.8181818127632141
            },
            {
                'latitude': -82.63619995117188,
                'point_count': 436,
                'longitude_origin': 0.0,
                'longitude_step': 0.8256880640983582
            },
            {
                'latitude': -82.70650482177734,
                'point_count': 432,
                'longitude_origin': 0.0,
                'longitude_step': 0.8333333134651184
            },
            {
                'latitude': -82.77680206298828,
                'point_count': 428,
                'longitude_origin': 0.0,
                'longitude_step': 0.84112149477005
            },
            {
                'latitude': -82.84709930419922,
                'point_count': 424,
                'longitude_origin': 0.0,
                'longitude_step': 0.849056601524353
            },
            {
                'latitude': -82.91739654541016,
                'point_count': 420,
                'longitude_origin': 0.0,
                'longitude_step': 0.8571428656578064
            },
            {
                'latitude': -82.9876937866211,
                'point_count': 416,
                'longitude_origin': 0.0,
                'longitude_step': 0.8653846383094788
            },
            {
                'latitude': -83.05799865722656,
                'point_count': 412,
                'longitude_origin': 0.0,
                'longitude_step': 0.8737863898277283
            },
            {
                'latitude': -83.1282958984375,
                'point_count': 408,
                'longitude_origin': 0.0,
                'longitude_step': 0.8823529481887817
            },
            {
                'latitude': -83.19859313964844,
                'point_count': 404,
                'longitude_origin': 0.0,
                'longitude_step': 0.8910890817642212
            },
            {
                'latitude': -83.26889038085938,
                'point_count': 400,
                'longitude_origin': 0.0,
                'longitude_step': 0.8999999761581421
            },
            {
                'latitude': -83.33918762207031,
                'point_count': 396,
                'longitude_origin': 0.0,
                'longitude_step': 0.9090909361839294
            },
            {
                'latitude': -83.40949249267578,
                'point_count': 392,
                'longitude_origin': 0.0,
                'longitude_step': 0.918367326259613
            },
            {
                'latitude': -83.47978973388672,
                'point_count': 388,
                'longitude_origin': 0.0,
                'longitude_step': 0.9278350472450256
            },
            {
                'latitude': -83.55008697509766,
                'point_count': 384,
                'longitude_origin': 0.0,
                'longitude_step': 0.9375
            },
            {
                'latitude': -83.6203842163086,
                'point_count': 380,
                'longitude_origin': 0.0,
                'longitude_step': 0.9473684430122375
            },
            {
                'latitude': -83.69068145751953,
                'point_count': 376,
                'longitude_origin': 0.0,
                'longitude_step': 0.957446813583374
            },
            {
                'latitude': -83.760986328125,
                'point_count': 372,
                'longitude_origin': 0.0,
                'longitude_step': 0.9677419066429138
            },
            {
                'latitude': -83.83128356933594,
                'point_count': 368,
                'longitude_origin': 0.0,
                'longitude_step': 0.97826087474823
            },
            {
                'latitude': -83.90158081054688,
                'point_count': 364,
                'longitude_origin': 0.0,
                'longitude_step': 0.9890109896659851
            },
            {
                'latitude': -83.97187805175781,
                'point_count': 360,
                'longitude_origin': 0.0,
                'longitude_step': 1.0
            },
            {
                'latitude': -84.04217529296875,
                'point_count': 356,
                'longitude_origin': 0.0,
                'longitude_step': 1.0112359523773193
            },
            {
                'latitude': -84.11248016357422,
                'point_count': 352,
                'longitude_origin': 0.0,
                'longitude_step': 1.0227272510528564
            },
            {
                'latitude': -84.18277740478516,
                'point_count': 348,
                'longitude_origin': 0.0,
                'longitude_step': 1.034482717514038
            },
            {
                'latitude': -84.2530746459961,
                'point_count': 344,
                'longitude_origin': 0.0,
                'longitude_step': 1.0465116500854492
            },
            {
                'latitude': -84.32337188720703,
                'point_count': 340,
                'longitude_origin': 0.0,
                'longitude_step': 1.058823585510254
            },
            {
                'latitude': -84.39366912841797,
                'point_count': 336,
                'longitude_origin': 0.0,
                'longitude_step': 1.0714285373687744
            },
            {
                'latitude': -84.46397399902344,
                'point_count': 332,
                'longitude_origin': 0.0,
                'longitude_step': 1.0843373537063599
            },
            {
                'latitude': -84.53427124023438,
                'point_count': 328,
                'longitude_origin': 0.0,
                'longitude_step': 1.097561001777649
            },
            {
                'latitude': -84.60456848144531,
                'point_count': 324,
                'longitude_origin': 0.0,
                'longitude_step': 1.1111111640930176
            },
            {
                'latitude': -84.67486572265625,
                'point_count': 320,
                'longitude_origin': 0.0,
                'longitude_step': 1.125
            },
            {
                'latitude': -84.74516296386719,
                'point_count': 316,
                'longitude_origin': 0.0,
                'longitude_step': 1.1392405033111572
            },
            {
                'latitude': -84.81546783447266,
                'point_count': 312,
                'longitude_origin': 0.0,
                'longitude_step': 1.1538461446762085
            },
            {
                'latitude': -84.8857650756836,
                'point_count': 308,
                'longitude_origin': 0.0,
                'longitude_step': 1.1688311100006104
            },
            {
                'latitude': -84.95606231689453,
                'point_count': 304,
                'longitude_origin': 0.0,
                'longitude_step': 1.1842105388641357
            },
            {
                'latitude': -85.02635955810547,
                'point_count': 300,
                'longitude_origin': 0.0,
                'longitude_step': 1.2000000476837158
            },
            {
                'latitude': -85.0966567993164,
                'point_count': 296,
                'longitude_origin': 0.0,
                'longitude_step': 1.2162162065505981
            },
            {
                'latitude': -85.16696166992188,
                'point_count': 292,
                'longitude_origin': 0.0,
                'longitude_step': 1.2328766584396362
            },
            {
                'latitude': -85.23725891113281,
                'point_count': 288,
                'longitude_origin': 0.0,
                'longitude_step': 1.25
            },
            {
                'latitude': -85.30755615234375,
                'point_count': 284,
                'longitude_origin': 0.0,
                'longitude_step': 1.2676056623458862
            },
            {
                'latitude': -85.37785339355469,
                'point_count': 280,
                'longitude_origin': 0.0,
                'longitude_step': 1.2857142686843872
            },
            {
                'latitude': -85.44815063476562,
                'point_count': 276,
                'longitude_origin': 0.0,
                'longitude_step': 1.3043478727340698
            },
            {
                'latitude': -85.5184555053711,
                'point_count': 272,
                'longitude_origin': 0.0,
                'longitude_step': 1.3235293626785278
            },
            {
                'latitude': -85.58875274658203,
                'point_count': 268,
                'longitude_origin': 0.0,
                'longitude_step': 1.3432835340499878
            },
            {
                'latitude': -85.65904998779297,
                'point_count': 264,
                'longitude_origin': 0.0,
                'longitude_step': 1.3636363744735718
            },
            {
                'latitude': -85.7293472290039,
                'point_count': 260,
                'longitude_origin': 0.0,
                'longitude_step': 1.384615421295166
            },
            {
                'latitude': -85.79964447021484,
                'point_count': 256,
                'longitude_origin': 0.0,
                'longitude_step': 1.40625
            },
            {
                'latitude': -85.86994934082031,
                'point_count': 252,
                'longitude_origin': 0.0,
                'longitude_step': 1.4285714626312256
            },
            {
                'latitude': -85.94024658203125,
                'point_count': 248,
                'longitude_origin': 0.0,
                'longitude_step': 1.451612949371338
            },
            {
                'latitude': -86.01054382324219,
                'point_count': 244,
                'longitude_origin': 0.0,
                'longitude_step': 1.4754098653793335
            },
            {
                'latitude': -86.08084106445312,
                'point_count': 240,
                'longitude_origin': 0.0,
                'longitude_step': 1.5
            },
            {
                'latitude': -86.15113830566406,
                'point_count': 236,
                'longitude_origin': 0.0,
                'longitude_step': 1.5254237651824951
            },
            {
                'latitude': -86.22144317626953,
                'point_count': 232,
                'longitude_origin': 0.0,
                'longitude_step': 1.5517241954803467
            },
            {
                'latitude': -86.29174041748047,
                'point_count': 228,
                'longitude_origin': 0.0,
                'longitude_step': 1.5789474248886108
            },
            {
                'latitude': -86.3620376586914,
                'point_count': 224,
                'longitude_origin': 0.0,
                'longitude_step': 1.6071428060531616
            },
            {
                'latitude': -86.43233489990234,
                'point_count': 220,
                'longitude_origin': 0.0,
                'longitude_step': 1.6363636255264282
            },
            {
                'latitude': -86.50263214111328,
                'point_count': 216,
                'longitude_origin': 0.0,
                'longitude_step': 1.6666666269302368
            },
            {
                'latitude': -86.57293701171875,
                'point_count': 212,
                'longitude_origin': 0.0,
                'longitude_step': 1.698113203048706
            },
            {
                'latitude': -86.64323425292969,
                'point_count': 208,
                'longitude_origin': 0.0,
                'longitude_step': 1.7307692766189575
            },
            {
                'latitude': -86.71353149414062,
                'point_count': 204,
                'longitude_origin': 0.0,
                'longitude_step': 1.7647058963775635
            },
            {
                'latitude': -86.78382873535156,
                'point_count': 200,
                'longitude_origin': 0.0,
                'longitude_step': 1.7999999523162842
            },
            {
                'latitude': -86.8541259765625,
                'point_count': 196,
                'longitude_origin': 0.0,
                'longitude_step': 1.836734652519226
            },
            {
                'latitude': -86.92443084716797,
                'point_count': 192,
                'longitude_origin': 0.0,
                'longitude_step': 1.875
            },
            {
                'latitude': -86.9947280883789,
                'point_count': 188,
                'longitude_origin': 0.0,
                'longitude_step': 1.914893627166748
            },
            {
                'latitude': -87.06502532958984,
                'point_count': 184,
                'longitude_origin': 0.0,
                'longitude_step': 1.95652174949646
            },
            {
                'latitude': -87.13532257080078,
                'point_count': 180,
                'longitude_origin': 0.0,
                'longitude_step': 2.0
            },
            {
                'latitude': -87.20561981201172,
                'point_count': 176,
                'longitude_origin': 0.0,
                'longitude_step': 2.045454502105713
            },
            {
                'latitude': -87.27592468261719,
                'point_count': 172,
                'longitude_origin': 0.0,
                'longitude_step': 2.0930233001708984
            },
            {
                'latitude': -87.34622192382812,
                'point_count': 168,
                'longitude_origin': 0.0,
                'longitude_step': 2.142857074737549
            },
            {
                'latitude': -87.41651916503906,
                'point_count': 164,
                'longitude_origin': 0.0,
                'longitude_step': 2.195122003555298
            },
            {
                'latitude': -87.48681640625,
                'point_count': 160,
                'longitude_origin': 0.0,
                'longitude_step': 2.25
            },
            {
                'latitude': -87.55711364746094,
                'point_count': 156,
                'longitude_origin': 0.0,
                'longitude_step': 2.307692289352417
            },
            {
                'latitude': -87.6274185180664,
                'point_count': 152,
                'longitude_origin': 0.0,
                'longitude_step': 2.3684210777282715
            },
            {
                'latitude': -87.69771575927734,
                'point_count': 148,
                'longitude_origin': 0.0,
                'longitude_step': 2.4324324131011963
            },
            {
                'latitude': -87.76801300048828,
                'point_count': 144,
                'longitude_origin': 0.0,
                'longitude_step': 2.5
            },
            {
                'latitude': -87.83831024169922,
                'point_count': 140,
                'longitude_origin': 0.0,
                'longitude_step': 2.5714285373687744
            },
            {
                'latitude': -87.90860748291016,
                'point_count': 136,
                'longitude_origin': 0.0,
                'longitude_step': 2.6470587253570557
            },
            {
                'latitude': -87.97891235351562,
                'point_count': 132,
                'longitude_origin': 0.0,
                'longitude_step': 2.7272727489471436
            },
            {
                'latitude': -88.04920959472656,
                'point_count': 128,
                'longitude_origin': 0.0,
                'longitude_step': 2.8125
            },
            {
                'latitude': -88.1195068359375,
                'point_count': 124,
                'longitude_origin': 0.0,
                'longitude_step': 2.903225898742676
            },
            {
                'latitude': -88.18980407714844,
                'point_count': 120,
                'longitude_origin': 0.0,
                'longitude_step': 3.0
            },
            {
                'latitude': -88.26010131835938,
                'point_count': 116,
                'longitude_origin': 0.0,
                'longitude_step': 3.1034483909606934
            },
            {
                'latitude': -88.33040618896484,
                'point_count': 112,
                'longitude_origin': 0.0,
                'longitude_step': 3.2142856121063232
            },
            {
                'latitude': -88.40070343017578,
                'point_count': 108,
                'longitude_origin': 0.0,
                'longitude_step': 3.3333332538604736
            },
            {
                'latitude': -88.47100067138672,
                'point_count': 104,
                'longitude_origin': 0.0,
                'longitude_step': 3.461538553237915
            },
            {
                'latitude': -88.54129791259766,
                'point_count': 100,
                'longitude_origin': 0.0,
                'longitude_step': 3.5999999046325684
            },
            {
                'latitude': -88.6115951538086,
                'point_count': 96,
                'longitude_origin': 0.0,
                'longitude_step': 3.75
            },
            {
                'latitude': -88.68190002441406,
                'point_count': 92,
                'longitude_origin': 0.0,
                'longitude_step': 3.91304349899292
            },
            {
                'latitude': -88.752197265625,
                'point_count': 88,
                'longitude_origin': 0.0,
                'longitude_step': 4.090909004211426
            },
            {
                'latitude': -88.82249450683594,
                'point_count': 84,
                'longitude_origin': 0.0,
                'longitude_step': 4.285714149475098
            },
            {
                'latitude': -88.89279174804688,
                'point_count': 80,
                'longitude_origin': 0.0,
                'longitude_step': 4.5
            },
            {
                'latitude': -88.96308898925781,
                'point_count': 76,
                'longitude_origin': 0.0,
                'longitude_step': 4.736842155456543
            },
            {
                'latitude': -89.03339385986328,
                'point_count': 72,
                'longitude_origin': 0.0,
                'longitude_step': 5.0
            },
            {
                'latitude': -89.10369110107422,
                'point_count': 68,
                'longitude_origin': 0.0,
                'longitude_step': 5.294117450714111
            },
            {
                'latitude': -89.17398834228516,
                'point_count': 64,
                'longitude_origin': 0.0,
                'longitude_step': 5.625
            },
            {
                'latitude': -89.2442855834961,
                'point_count': 60,
                'longitude_origin': 0.0,
                'longitude_step': 6.0
            },
            {
                'latitude': -89.31458282470703,
                'point_count': 56,
                'longitude_origin': 0.0,
                'longitude_step': 6.4285712242126465
            },
            {
                'latitude': -89.3848876953125,
                'point_count': 52,
                'longitude_origin': 0.0,
                'longitude_step': 6.92307710647583
            },
            {
                'latitude': -89.45518493652344,
                'point_count': 48,
                'longitude_origin': 0.0,
                'longitude_step': 7.5
            },
            {
                'latitude': -89.52548217773438,
                'point_count': 44,
                'longitude_origin': 0.0,
                'longitude_step': 8.181818008422852
            },
            {
                'latitude': -89.59577941894531,
                'point_count': 40,
                'longitude_origin': 0.0,
                'longitude_step': 9.0
            },
            {
                'latitude': -89.66607666015625,
                'point_count': 36,
                'longitude_origin': 0.0,
                'longitude_step': 10.0
            },
            {
                'latitude': -89.73638153076172,
                'point_count': 32,
                'longitude_origin': 0.0,
                'longitude_step': 11.25
            },
            {
                'latitude': -89.80667877197266,
                'point_count': 28,
                'longitude_origin': 0.0,
                'longitude_step': 12.857142448425293
            },
            {
                'latitude': -89.8769760131836,
                'point_count': 24,
                'longitude_origin': 0.0,
                'longitude_step': 15.0
            },
            {
                'latitude': -89.94727325439453,
                'point_count': 20,
                'longitude_origin': 0.0,
                'longitude_step': 18.0
            }
        ],
        'subset_segments': NULL
    }
}, spatial_axes := ['lon'], include_source := true)) TO '/home/blizhan/repo/github/duckomo/build/hres-o1280/validation-20261010-r2/static-explicit.csv' (HEADER, NULL 'NULL');
COPY (SELECT * FROM duckomo_last_scan_metrics()) TO '/home/blizhan/repo/github/duckomo/build/hres-o1280/validation-20261010-r2/static-explicit.metrics.csv' (HEADER);
