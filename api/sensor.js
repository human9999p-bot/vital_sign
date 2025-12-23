import { neon } from '@neondatabase/serverless';

// اتصال Neon
const client = new neon(process.env.DATABASE_URL);

export default async function handler(req, res) {
  try {
    if (req.method === 'POST') {
      // استقبال البيانات من ESP32 أو أي client
      const { device_id, heartrate, spo2 } = req.body || {};

      if (!device_id || heartrate == null || spo2 == null) {
        return res.status(400).json({
          message: 'Missing sensor data',
          received: req.body
        });
      }

      if (typeof heartrate !== 'number' || typeof spo2 !== 'number') {
        return res.status(400).json({
          message: 'Invalid data types',
          received: req.body
        });
      }

      // حفظ البيانات في Neon
      await client.query(
        `INSERT INTO sensor_data (device_id, heartrate, spo2, time)
         VALUES ($1, $2, $3, NOW())`,
        [device_id, heartrate, spo2]
      );

      return res.status(200).json({ message: 'Data saved successfully' });

    } else if (req.method === 'GET') {
      // جلب آخر 50 سجل
      const result = await client.query(
        `SELECT device_id, heartrate, spo2, time
         FROM sensor_data
         ORDER BY time ASC`
      );

      const rows = result.rows ? result.rows : result;

      const data = rows.map(r => ({
        device_id: r.device_id,
        heartrate: r.heartrate,
        spo2: r.spo2,
        time: r.time ? r.time : new Date().toISOString()
      }));

      return res.status(200).json(data);

    } else {
      return res.status(405).json({ message: 'Method not allowed' });
    }
  } catch (err) {
    console.error('Server error:', err);
    return res.status(500).json({
      message: 'Server error',
      detail: err.message
    });
  }
}
